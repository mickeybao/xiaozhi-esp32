/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <esp_pthread.h>
#include <memory>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "display.h"
#include "oled_display.h"
#include "board.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "lvgl_display.h"
#include "ota.h"
#include "audio/demuxer/ogg_demuxer.h"
#include "protocols/protocol.h"
#include "assets/lang_config.h"

#define TAG "MCP"

namespace {

void PlayVoiceTimerAlarmTask(void*) {
    auto& app = Application::GetInstance();
    for (int i = 0; i < 9; ++i) {
        app.PlaySound(Lang::Sounds::OGG_EXCLAMATION);
        if (i < 8) {
            vTaskDelay(pdMS_TO_TICKS(450));
        }
    }
    vTaskDelete(nullptr);
}

class VoiceTimerManager {
public:
    static VoiceTimerManager& GetInstance() {
        static VoiceTimerManager instance;
        return instance;
    }

    std::string Start(int seconds) {
        if (seconds <= 0) {
            return "Invalid timer duration";
        }

        EnsureTimers();

        if (seconds > kMaxDurationSeconds) {
            seconds = kMaxDurationSeconds;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = true;
            duration_seconds_ = seconds;
            end_time_us_ = esp_timer_get_time() + static_cast<int64_t>(seconds) * kSecondUs;
        }

        esp_timer_stop(tick_timer_);
        esp_timer_stop(restore_timer_);
        ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer_, kSecondUs));
        ShowCountdown(seconds);

        return std::string("Timer started: ") + FormatRemaining(seconds);
    }

    std::string Cancel() {
        bool was_active = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            was_active = active_;
            active_ = false;
        }

        if (tick_timer_ != nullptr) {
            esp_timer_stop(tick_timer_);
        }
        if (restore_timer_ != nullptr) {
            esp_timer_stop(restore_timer_);
        }

        RestoreChat();
        return was_active ? "Timer cancelled" : "No active timer";
    }

private:
    static constexpr int kMaxDurationSeconds = 24 * 60 * 60;
    static constexpr int64_t kSecondUs = 1000000;
    static constexpr int64_t kRestoreDelayUs = 5 * kSecondUs;

    std::mutex mutex_;
    esp_timer_handle_t tick_timer_ = nullptr;
    esp_timer_handle_t restore_timer_ = nullptr;
    bool active_ = false;
    int duration_seconds_ = 0;
    int64_t end_time_us_ = 0;

    VoiceTimerManager() = default;

    void EnsureTimers() {
        if (tick_timer_ == nullptr) {
            esp_timer_create_args_t tick_args = {
                .callback = &VoiceTimerManager::TickTimerCallback,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "voice_timer_tick",
                .skip_unhandled_events = true,
            };
            ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer_));
        }

        if (restore_timer_ == nullptr) {
            esp_timer_create_args_t restore_args = {
                .callback = &VoiceTimerManager::RestoreTimerCallback,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "voice_timer_done",
                .skip_unhandled_events = true,
            };
            ESP_ERROR_CHECK(esp_timer_create(&restore_args, &restore_timer_));
        }
    }

    static void TickTimerCallback(void* arg) {
        static_cast<VoiceTimerManager*>(arg)->OnTick();
    }

    static void RestoreTimerCallback(void* arg) {
        static_cast<VoiceTimerManager*>(arg)->RestoreChat();
    }

    void OnTick() {
        int remaining_seconds = 0;
        bool finished = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_) {
                return;
            }

            int64_t remaining_us = end_time_us_ - esp_timer_get_time();
            if (remaining_us <= 0) {
                active_ = false;
                finished = true;
            } else {
                remaining_seconds = static_cast<int>((remaining_us + kSecondUs - 1) / kSecondUs);
            }
        }

        if (finished) {
            esp_timer_stop(tick_timer_);
            ShowFinished();
            esp_timer_stop(restore_timer_);
            ESP_ERROR_CHECK(esp_timer_start_once(restore_timer_, kRestoreDelayUs));
            return;
        }

        ShowCountdown(remaining_seconds);
    }

    static std::string FormatRemaining(int seconds) {
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int secs = seconds % 60;

        char buffer[16];
        if (hours > 0) {
            snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, secs);
        } else {
            snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);
        }
        return buffer;
    }

    void ShowCountdown(int remaining_seconds) {
        std::string time_text = FormatRemaining(remaining_seconds);
        Application::GetInstance().Schedule([time_text]() {
            static constexpr const char* kCountdownText = "\xE5\x80\x92\xE8\xAE\xA1\xE6\x97\xB6";
            auto& board = Board::GetInstance();
            if (auto backlight = board.GetBacklight()) {
                backlight->RestoreBrightness();
            }

            auto display = board.GetDisplay();
            if (display == nullptr) {
                return;
            }

            display->SetPowerSaveMode(false);
            display->SetStatus(kCountdownText);
            display->SetEmotion("neutral");
            std::string message = std::string(kCountdownText) + " " + time_text;
            display->SetChatMessage("system", message.c_str());
        });
    }

    void ShowFinished() {
        Application::GetInstance().Schedule([]() {
            static constexpr const char* kReminderText = "\xE6\x8F\x90\xE9\x86\x92";
            static constexpr const char* kTimeUpText = "\xE6\x97\xB6\xE9\x97\xB4\xE5\x88\xB0\xE5\x95\xA6";
            auto& app = Application::GetInstance();
            auto& board = Board::GetInstance();
            if (auto backlight = board.GetBacklight()) {
                backlight->RestoreBrightness();
            }

            auto display = board.GetDisplay();
            if (display != nullptr) {
                display->SetPowerSaveMode(false);
                display->SetStatus(kReminderText);
                display->SetEmotion("surprised");
                display->SetChatMessage("system", kTimeUpText);
            }
            xTaskCreate(PlayVoiceTimerAlarmTask, "voice_timer_alarm", 2048, nullptr, 3, nullptr);
        });
    }

    void RestoreChat() {
        Application::GetInstance().Schedule([]() {
            Application::GetInstance().DismissAlert();
        });
    }
};

class RadioStreamManager {
public:
    static RadioStreamManager& GetInstance() {
        static RadioStreamManager instance;
        return instance;
    }

    std::string Start(const std::string& url, const std::string& name) {
        std::string stream_url = url;
        std::string station_name = name.empty() ? "网络收音机" : name;
        if (stream_url.empty()) {
            auto resolved = ResolveStationUrl(station_name);
            if (resolved.empty()) {
                return "没有找到可直接播放的 Ogg Opus 电台。可以换一个电台名称，或在后台把 MP3/AAC 电台转码成 Ogg Opus。";
            }
            stream_url = resolved;
        }
        if (stream_url.rfind("http://", 0) != 0 && stream_url.rfind("https://", 0) != 0) {
            return "网络收音机 URL 必须以 http:// 或 https:// 开头";
        }

        Stop();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stream_id_++;
            url_ = stream_url;
            name_ = station_name;
            stop_requested_ = false;
        }

        auto display = Board::GetInstance().GetDisplay();
        display->SetEmotion("happy");
        display->SetStatus("收音机");
        display->SetChatMessage("system", ("正在播放 " + name_).c_str());

        BaseType_t ok = xTaskCreate([](void* arg) {
            static_cast<RadioStreamManager*>(arg)->StreamTask();
            vTaskDelete(nullptr);
        }, "radio_stream", 4096 * 4, this, 2, &task_handle_);

        if (ok != pdPASS) {
            task_handle_ = nullptr;
            return "启动网络收音机任务失败";
        }
        return "Radio started";
    }

    std::string Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_requested_ = true;
        }
        Application::GetInstance().GetAudioService().ResetDecoder();
        auto display = Board::GetInstance().GetDisplay();
        display->SetChatMessage("system", "网络收音机已停止");
        display->SetStatus(Lang::Strings::STANDBY);
        return "Radio stopped";
    }

    std::string List(const std::string& name, const std::string& country,
                     const std::string& language, const std::string& tag) {
        return ListStations(name, country, language, tag);
    }

private:
    std::mutex mutex_;
    TaskHandle_t task_handle_ = nullptr;
    bool stop_requested_ = false;
    uint32_t stream_id_ = 0;
    std::string url_;
    std::string name_;

    RadioStreamManager() = default;

    std::string UrlEncode(const std::string& value) {
        static const char hex[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size() * 3);
        for (unsigned char c : value) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded.push_back(static_cast<char>(c));
            } else if (c == ' ') {
                encoded.push_back('+');
            } else {
                encoded.push_back('%');
                encoded.push_back(hex[c >> 4]);
                encoded.push_back(hex[c & 0x0F]);
            }
        }
        return encoded;
    }

    bool SameTextIgnoreCase(const char* left, const char* right) {
        if (left == nullptr || right == nullptr) {
            return false;
        }
        while (*left != '\0' && *right != '\0') {
            if (std::tolower(static_cast<unsigned char>(*left)) !=
                std::tolower(static_cast<unsigned char>(*right))) {
                return false;
            }
            left++;
            right++;
        }
        return *left == '\0' && *right == '\0';
    }

    std::string ResolveStationUrl(const std::string& name) {
        std::string api_url = BuildStationSearchUrl(name, "", "", "", 8, name.empty() || name == "网络收音机");
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetTimeout(8000);
        http->SetHeader("User-Agent", "XiaoZhiWave/2.2.16");
        http->SetHeader("Accept", "application/json");
        if (!http->Open("GET", api_url)) {
            return "";
        }
        if (http->GetStatusCode() < 200 || http->GetStatusCode() >= 300) {
            http->Close();
            return "";
        }

        std::string body = http->ReadAll();
        http->Close();
        cJSON* root = cJSON_Parse(body.c_str());
        if (!cJSON_IsArray(root)) {
            if (root != nullptr) {
                cJSON_Delete(root);
            }
            return "";
        }

        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, root) {
            const cJSON* codec = cJSON_GetObjectItem(item, "codec");
            const cJSON* url_resolved = cJSON_GetObjectItem(item, "url_resolved");
            const cJSON* raw_url = cJSON_GetObjectItem(item, "url");
            const char* candidate = cJSON_IsString(url_resolved) && url_resolved->valuestring[0] != '\0'
                ? url_resolved->valuestring
                : (cJSON_IsString(raw_url) ? raw_url->valuestring : "");
            if (candidate == nullptr || candidate[0] == '\0') {
                continue;
            }
            if (cJSON_IsString(codec) &&
                (SameTextIgnoreCase(codec->valuestring, "OPUS") || SameTextIgnoreCase(codec->valuestring, "OGG"))) {
                std::string result = candidate;
                cJSON_Delete(root);
                return result;
            }
        }

        cJSON_Delete(root);
        return "";
    }

    std::string BuildStationSearchUrl(const std::string& name, const std::string& country,
                                      const std::string& language, const std::string& tag,
                                      int limit, bool random) {
        std::string api_url = "https://de1.api.radio-browser.info/json/stations/search?hidebroken=true&limit=" +
            std::to_string(limit);
        if (random) {
            api_url += "&order=random";
        } else {
            api_url += "&order=clickcount&reverse=true";
        }
        api_url += "&codec=OPUS";
        if (!name.empty() && name != "网络收音机") {
            api_url += "&name=" + UrlEncode(name);
        }
        if (!country.empty()) {
            api_url += "&country=" + UrlEncode(country);
        }
        if (!language.empty()) {
            api_url += "&language=" + UrlEncode(language);
        }
        if (!tag.empty()) {
            api_url += "&tag=" + UrlEncode(tag);
        }
        return api_url;
    }

    std::string ListStations(const std::string& name, const std::string& country,
                             const std::string& language, const std::string& tag) {
        std::string api_url = BuildStationSearchUrl(name, country, language, tag, 6, name.empty() && country.empty() && language.empty() && tag.empty());
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetTimeout(8000);
        http->SetHeader("User-Agent", "XiaoZhiWave/2.2.16");
        http->SetHeader("Accept", "application/json");
        if (!http->Open("GET", api_url)) {
            return "查询电台目录失败";
        }
        if (http->GetStatusCode() < 200 || http->GetStatusCode() >= 300) {
            http->Close();
            return "电台目录响应异常";
        }

        std::string body = http->ReadAll();
        http->Close();
        cJSON* root = cJSON_Parse(body.c_str());
        if (!cJSON_IsArray(root)) {
            if (root != nullptr) {
                cJSON_Delete(root);
            }
            return "电台目录数据解析失败";
        }

        std::string result = "可直接播放的 Ogg Opus 电台：";
        int count = 0;
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, root) {
            const cJSON* station_name = cJSON_GetObjectItem(item, "name");
            const cJSON* station_country = cJSON_GetObjectItem(item, "country");
            const cJSON* codec = cJSON_GetObjectItem(item, "codec");
            const cJSON* bitrate = cJSON_GetObjectItem(item, "bitrate");
            if (!cJSON_IsString(station_name) || station_name->valuestring[0] == '\0') {
                continue;
            }
            result += "\n";
            result += std::to_string(count + 1);
            result += ". ";
            result += station_name->valuestring;
            if (cJSON_IsString(station_country) && station_country->valuestring[0] != '\0') {
                result += " / ";
                result += station_country->valuestring;
            }
            if (cJSON_IsString(codec)) {
                result += " / ";
                result += codec->valuestring;
            }
            if (cJSON_IsNumber(bitrate) && bitrate->valueint > 0) {
                result += " / ";
                result += std::to_string(bitrate->valueint);
                result += "kbps";
            }
            count++;
        }
        cJSON_Delete(root);

        if (count == 0) {
            return "没有找到可直接播放的 Ogg Opus 电台。可以换关键词，或使用后台转码 MP3/AAC 电台。";
        }
        result += "\n你可以说：播放第一个，或播放上面某个电台名。";
        return result;
    }

    bool ShouldStop(uint32_t stream_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return stop_requested_ || stream_id != stream_id_;
    }

    void StreamTask() {
        std::string url;
        std::string name;
        uint32_t stream_id = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            url = url_;
            name = name_;
            stream_id = stream_id_;
        }

        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetTimeout(20000);
        http->SetHeader("Accept", "audio/ogg, application/ogg, audio/opus, */*");
        if (!http->Open("GET", url)) {
            ShowError("电台连接失败");
            MarkStopped(stream_id);
            return;
        }

        int status_code = http->GetStatusCode();
        if (status_code < 200 || status_code >= 300) {
            http->Close();
            ShowError("电台响应异常");
            MarkStopped(stream_id);
            return;
        }

        auto& audio_service = Application::GetInstance().GetAudioService();
        auto demuxer = std::make_unique<OggDemuxer>();
        demuxer->OnDemuxerFinished([&audio_service](const uint8_t* data, int sample_rate, size_t size) {
            auto packet = std::make_unique<AudioStreamPacket>();
            packet->sample_rate = sample_rate;
            packet->frame_duration = 60;
            packet->payload.resize(size);
            std::memcpy(packet->payload.data(), data, size);
            audio_service.PushPacketToDecodeQueue(std::move(packet), true);
        });
        demuxer->Reset();

        char buffer[1024];
        while (!ShouldStop(stream_id)) {
            int ret = http->Read(buffer, sizeof(buffer));
            if (ret < 0) {
                ShowError("电台读取失败");
                break;
            }
            if (ret == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            demuxer->Process(reinterpret_cast<const uint8_t*>(buffer), ret);
        }

        http->Close();
        MarkStopped(stream_id);
    }

    void ShowError(const char* message) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetEmotion("confused");
        display->SetChatMessage("system", message);
        display->ShowNotification(message, 3000);
    }

    void MarkStopped(uint32_t stream_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stream_id != stream_id_) {
            return;
        }
        task_handle_ = nullptr;
        stop_requested_ = true;
    }
};

} // namespace

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **閲嶈** 涓轰簡鎻愬崌鍝嶅簲閫熷害锛屾垜浠妸甯哥敤鐨勫伐鍏锋斁鍦ㄥ墠闈紝鍒╃敤 prompt cache 鐨勭壒鎬с€?

    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.

    AddTool("self.get_device_status",
        "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n"
        "Use this tool for: \n"
        "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"
        "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
        });

    AddTool("self.timer.start",
        "Start or replace a countdown timer on the device. Use this when the user asks for a timer, countdown, alarm after a duration, "
        "voice timer, 定时提醒, 倒计时, or phrases like 定时1分钟 / 一分钟后提醒我. "
        "Convert the requested duration to seconds before calling this tool. The screen shows the countdown; "
        "when time is up, the device plays a reminder sound and returns to the chat screen.",
        PropertyList({
            Property("seconds", kPropertyTypeInteger, 1, 24 * 60 * 60)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            int seconds = properties["seconds"].value<int>();
            return VoiceTimerManager::GetInstance().Start(seconds);
        });

    AddTool("self.timer.cancel",
        "Cancel the active countdown timer. Use this when the user asks to cancel, stop, or clear the timer, 取消定时, 停止倒计时.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            return VoiceTimerManager::GetInstance().Cancel();
        });

    AddTool("self.radio.play",
        "Play an internet radio stream on the device. Use this when the user asks to play internet radio, 网络收音机, 电台, or 在线电台. "
        "If url is empty, search a public radio directory by name and prefer Ogg Opus streams. "
        "This first implementation can directly play Ogg Opus streams; MP3/AAC stations need server-side transcoding.",
        PropertyList({
            Property("url", kPropertyTypeString, std::string("")),
            Property("name", kPropertyTypeString, std::string("网络收音机"))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            auto name = properties["name"].value<std::string>();
            return RadioStreamManager::GetInstance().Start(url, name);
        });

    AddTool("self.radio.list",
        "List playable internet radio stations. Use this when the user asks what stations are available, 有哪些电台, 推荐几个电台, "
        "or wants to test radio playback but does not know station names. This lists Ogg Opus stations that the current firmware can directly play.",
        PropertyList({
            Property("name", kPropertyTypeString, std::string("")),
            Property("country", kPropertyTypeString, std::string("")),
            Property("language", kPropertyTypeString, std::string("")),
            Property("tag", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto name = properties["name"].value<std::string>();
            auto country = properties["country"].value<std::string>();
            auto language = properties["language"].value<std::string>();
            auto tag = properties["tag"].value<std::string>();
            return RadioStreamManager::GetInstance().List(name, country, language, tag);
        });

    AddTool("self.radio.stop",
        "Stop the currently playing internet radio stream. Use this when the user says stop radio, 关闭电台, 停止网络收音机, or 不听了.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            return RadioStreamManager::GetInstance().Stop();
        });

    AddTool("self.music.play_test",
        "Play the XiaoZhi music proxy test stream on the device. Use this when the user asks to test music playback, "
        "测试音乐播放, 测试歌曲播放, or play the current MP3 transcoding test file. "
        "The default URL points to the deployed music proxy test stream, but url and name can be overridden.",
        PropertyList({
            Property("url", kPropertyTypeString, std::string("http://miaomiao.atchain.cn/stream?file=1.mp3")),
            Property("name", kPropertyTypeString, std::string("测试歌曲"))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            auto name = properties["name"].value<std::string>();
            return RadioStreamManager::GetInstance().Start(url, name);
        });

    AddTool("self.system.check_and_upgrade_firmware",
        "Check the configured OTA server for a newer firmware and install it if available. "
        "Use this when the user asks to update, upgrade, OTA, check for a firmware update, 鍗囩骇鍥轰欢, 绯荤粺鍗囩骇, 妫€鏌ユ洿鏂? or OTA鍗囩骇 by voice. "
        "This may download firmware and reboot the device, so you must ask the user to confirm before calling it.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            (void)properties;
            ESP_LOGW(TAG, "User requested OTA check and upgrade");

            xTaskCreate([](void* arg) {
                auto& app = Application::GetInstance();
                auto display = Board::GetInstance().GetDisplay();

                app.Schedule([display]() {
                    display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);
                    display->SetChatMessage("system", Lang::Strings::CHECKING_NEW_VERSION);
                });

                Ota ota;
                esp_err_t err = ota.CheckVersion();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Voice OTA check failed: %s", esp_err_to_name(err));
                    app.Schedule([display, err]() {
                        char message[96];
                        snprintf(message, sizeof(message), "OTA check failed: %s", esp_err_to_name(err));
                        display->ShowNotification(message, 3000);
                        display->SetChatMessage("system", message);
                    });
                    vTaskDelete(nullptr);
                    return;
                }

                ota.MarkCurrentVersionValid();
                if (!ota.HasNewVersion()) {
                    std::string message = std::string("Already latest: ") + ota.GetCurrentVersion();
                    app.Schedule([display, message]() {
                        display->ShowNotification(message.c_str(), 3000);
                        display->SetChatMessage("system", message.c_str());
                    });
                    vTaskDelete(nullptr);
                    return;
                }

                auto firmware_url = ota.GetFirmwareUrl();
                auto firmware_version = ota.GetFirmwareVersion();
                bool success = app.UpgradeFirmware(firmware_url, firmware_version);
                if (!success) {
                    ESP_LOGE(TAG, "Voice OTA upgrade failed");
                }
                vTaskDelete(nullptr);
            }, "voice_ota", 4096 * 2, nullptr, 2, nullptr);

            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                return true;
            });
    }

#ifdef HAVE_LVGL
    auto display = board.GetDisplay();
    if (display && display->GetTheme() != nullptr) {
        AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto theme_name = properties["theme"].value<std::string>();
                auto& theme_manager = LvglThemeManager::GetInstance();
                auto theme = theme_manager.GetTheme(theme_name);
                if (theme != nullptr) {
                    display->SetTheme(theme);
                    return true;
                }
                return false;
            });
    }

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Always remember you have a camera. If the user asks you to see something, use this tool to take a photo and then explain it.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                // Lower the priority to do the camera capture
                TaskPriorityReset priority_reset(1);

                if (!camera->Capture()) {
                    throw std::runtime_error("Failed to capture photo");
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });
    }
#endif

    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools() {
    // System tools
    AddUserOnlyTool("self.get_system_info",
        "Get the system information",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            return board.GetSystemInfoJson();
        });

    AddUserOnlyTool("self.reboot", "Reboot the system",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            app.Schedule([&app]() {
                ESP_LOGW(TAG, "User requested reboot");
                vTaskDelay(pdMS_TO_TICKS(1000));

                app.Reboot();
            });
            return true;
        });

    // Firmware upgrade
    AddUserOnlyTool("self.upgrade_firmware", "Upgrade firmware from a specific URL. This will download and install the firmware, then reboot the device.",
        PropertyList({
            Property("url", kPropertyTypeString, "The URL of the firmware binary file to download and install")
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            ESP_LOGI(TAG, "User requested firmware upgrade from URL: %s", url.c_str());

            xTaskCreate([](void* arg) {
                std::unique_ptr<std::string> url(static_cast<std::string*>(arg));
                auto& app = Application::GetInstance();
                bool success = app.UpgradeFirmware(*url);
                if (!success) {
                    ESP_LOGE(TAG, "Firmware upgrade failed");
                }
                vTaskDelete(nullptr);
            }, "manual_ota", 4096 * 2, new std::string(url), 2, nullptr);
            
            return true;
        });

    // Display control
#ifdef HAVE_LVGL
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display) {
        AddUserOnlyTool("self.screen.get_info", "Information about the screen, including width, height, etc.",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "width", display->width());
                cJSON_AddNumberToObject(json, "height", display->height());
                if (dynamic_cast<OledDisplay*>(display)) {
                    cJSON_AddBoolToObject(json, "monochrome", true);
                } else {
                    cJSON_AddBoolToObject(json, "monochrome", false);
                }
                return json;
            });

#if CONFIG_LV_USE_SNAPSHOT
        AddUserOnlyTool("self.screen.snapshot", "Snapshot the screen and upload it to a specific URL",
            PropertyList({
                Property("url", kPropertyTypeString),
                Property("quality", kPropertyTypeInteger, 80, 1, 100)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto quality = properties["quality"].value<int>();

                std::string jpeg_data;
                if (!display->SnapshotToJpeg(jpeg_data, quality)) {
                    throw std::runtime_error("Failed to snapshot screen");
                }

                ESP_LOGI(TAG, "Upload snapshot %u bytes to %s", jpeg_data.size(), url.c_str());
                
                // 鏋勯€爉ultipart/form-data璇锋眰浣?
                std::string boundary = "----ESP32_SCREEN_SNAPSHOT_BOUNDARY";
                
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
                if (!http->Open("POST", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                {
                    // 鏂囦欢瀛楁澶撮儴
                    std::string file_header;
                    file_header += "--" + boundary + "\r\n";
                    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"screenshot.jpg\"\r\n";
                    file_header += "Content-Type: image/jpeg\r\n";
                    file_header += "\r\n";
                    http->Write(file_header.c_str(), file_header.size());
                }

                // JPEG鏁版嵁
                http->Write((const char*)jpeg_data.data(), jpeg_data.size());

                {
                    // multipart灏鹃儴
                    std::string multipart_footer;
                    multipart_footer += "\r\n--" + boundary + "--\r\n";
                    http->Write(multipart_footer.c_str(), multipart_footer.size());
                }
                http->Write("", 0);

                if (http->GetStatusCode() != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(http->GetStatusCode()));
                }
                std::string result = http->ReadAll();
                http->Close();
                ESP_LOGI(TAG, "Snapshot screen result: %s", result.c_str());
                return true;
            });
        
        AddUserOnlyTool("self.screen.preview_image", "Preview an image on the screen",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

                if (!http->Open("GET", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                int status_code = http->GetStatusCode();
                if (status_code != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(status_code));
                }

                size_t content_length = http->GetBodyLength();
                char* data = (char*)heap_caps_malloc(content_length, MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    throw std::runtime_error("Failed to allocate memory for image: " + url);
                }
                size_t total_read = 0;
                while (total_read < content_length) {
                    int ret = http->Read(data + total_read, content_length - total_read);
                    if (ret < 0) {
                        heap_caps_free(data);
                        throw std::runtime_error("Failed to download image: " + url);
                    }
                    if (ret == 0) {
                        break;
                    }
                    total_read += ret;
                }
                http->Close();

                auto image = std::make_unique<LvglAllocatedImage>(data, content_length);
                display->SetPreviewImage(std::move(image));
                return true;
            });
#endif // CONFIG_LV_USE_SNAPSHOT
    }
#endif // HAVE_LVGL

    // Assets download url (always registered 鈥?Settings storage works regardless of partition layout)
    AddUserOnlyTool("self.assets.set_download_url", "Set the download url for the assets",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                Settings settings("assets", true);
                settings.SetString("download_url", url);
                return true;
            });
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        bool list_user_only_tools = false;
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
            auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
            if (cJSON_IsBool(with_user_tools)) {
                list_user_only_tools = with_user_tools->valueint == 1;
            }
        }
        GetToolsList(id_int, cursor_str, list_user_only_tools);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 濡傛灉鎴戜滑杩樻病鏈夋壘鍒拌捣濮嬩綅缃紝缁х画鎼滅储
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }

        if (!list_user_only_tools && (*it)->user_only()) {
            ++it;
            continue;
        }
        
        // 娣诲姞tool鍓嶆鏌ュぇ灏?
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 濡傛灉娣诲姞杩欎釜tool浼氳秴鍑哄ぇ灏忛檺鍒讹紝璁剧疆next_cursor骞堕€€鍑哄惊鐜?
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 濡傛灉娌℃湁娣诲姞浠讳綍tool锛岃繑鍥為敊璇?
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Use main thread to call the tool
    auto& app = Application::GetInstance();
    app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
}
