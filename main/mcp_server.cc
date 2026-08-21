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
#include <esp_audio_simple_dec.h>
#include <esp_mp3_dec.h>
#include <esp_ae_rate_cvt.h>

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

    bool IsRunning() {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
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
            auto resolved = ResolvePresetUrl(station_name);
            if (resolved.empty()) {
                return "未找到内置电台。请选择音乐、新闻、交通或天气类电台，也可以说具体电台名称。";
            }
            stream_url = resolved;
        }
        if (stream_url.rfind("http://", 0) != 0 && stream_url.rfind("https://", 0) != 0) {
            return "网络收音机 URL 必须以 http:// 或 https:// 开头";
        }

        Stop(false);
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

    std::string Stop(bool update_display = true) {
        bool was_running = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            was_running = !stop_requested_ || task_handle_ != nullptr;
            stop_requested_ = true;
        }
        if (!was_running) {
            return "Radio already stopped";
        }
        Application::GetInstance().GetAudioService().ResetDecoder();
        if (update_display) {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "网络收音机已停止");
            display->SetStatus(Lang::Strings::STANDBY);
        }
        return "Radio stopped";
    }

    bool IsRunning() {
        std::lock_guard<std::mutex> lock(mutex_);
        return task_handle_ != nullptr && !stop_requested_;
    }

    std::string List(const std::string& name, const std::string& country,
                     const std::string& language, const std::string& tag) {
        (void)country;
        (void)language;
        (void)tag;
        auto presets = ListPresetStations(name);
        return presets.empty() ? "未找到匹配的内置电台。可选分类：音乐、新闻、交通、天气。" : presets;
    }

private:
    struct RadioPreset {
        const char* name;
        const char* keywords;
        const char* url;
    };

    static constexpr RadioPreset kPresets[] = {
        {"中国之声", "中央广播 中国之声 CNR1 新闻台", "https://lhttp.qtfm.cn/live/15318317/64k.mp3"},
        {"国际新闻", "国际新闻 新闻台", "https://lhttp.qtfm.cn/live/20500172/64k.mp3"},
        {"上海新闻广播", "上海新闻广播 上海新闻 新闻台", "https://lhttp.qingting.fm/live/270/64k.mp3"},
        {"长三角之声", "长三角之声 新闻台", "https://lhttp.qingting.fm/live/275/64k.mp3"},
        {"第一财经广播", "第一财经广播 第一财经 新闻台", "https://lhttp.qingting.fm/live/276/64k.mp3"},
        {"音乐之声", "音乐之声 音乐台", "https://lhttp.qingting.fm/live/4804/64k.mp3"},
        {"清晨音乐台", "清晨 音乐台", "http://lhttp.qingting.fm/live/4915/64k.mp3"},
        {"两广之声音乐台", "两广之声 两广 音乐台", "https://lhttp.qtfm.cn/live/20500149/64k.mp3"},
        {"动听音乐台", "动听 音乐台", "https://lhttp-hw.qtfm.cn/live/5022107/64k.mp3"},
        {"中国流行民乐", "中国流行民乐 中国民乐 流行民乐 民乐 音乐台", "https://radio.chinesemusicworld.com/chinesemusic.mp3"},
        {"上海交通广播", "上海交通广播 上海交通 交通台", "http://lhttp.qingting.fm/live/266/64k.mp3"},
        {"上海天气台", "上海天气台 上海天气 天气台", "https://lhttp.qtfm.cn/live/20500176/64k.mp3"},
        {"法国新闻", "法国新闻 法国 RMC 新闻台", "https://audio.bfmtv.com/rmcradio_128.mp3"},
        {"美国公共新闻", "美国公共新闻 美国新闻 WNYC 纽约公共广播", "https://fm939.wnyc.org/wnycfm"},
        {"俄语新闻谈话", "俄语新闻谈话 俄语新闻 Radio Zvezda", "https://icecast-zvezda.mediacdn.ru/radio/zvezda/zvezda_128"},
    };

    std::mutex mutex_;
    TaskHandle_t task_handle_ = nullptr;
    bool stop_requested_ = false;
    uint32_t stream_id_ = 0;
    std::string url_;
    std::string name_;

    RadioStreamManager() = default;

    bool ContainsIgnoreCase(const std::string& text, const std::string& query) {
        if (query.empty()) {
            return true;
        }
        auto lower = [](unsigned char value) { return static_cast<char>(std::tolower(value)); };
        std::string lowered_text(text.size(), '\0');
        std::string lowered_query(query.size(), '\0');
        std::transform(text.begin(), text.end(), lowered_text.begin(), lower);
        std::transform(query.begin(), query.end(), lowered_query.begin(), lower);
        return lowered_text.find(lowered_query) != std::string::npos;
    }

    std::string ResolvePresetUrl(const std::string& name) {
        if (name.empty() || name == "网络收音机" || name == "电台") {
            return kPresets[0].url;
        }
        for (const auto& preset : kPresets) {
            if (ContainsIgnoreCase(preset.name, name) ||
                ContainsIgnoreCase(preset.keywords, name)) {
                return preset.url;
            }
        }
        if (ContainsIgnoreCase(name, "音乐")) {
            return kPresets[5].url;
        }
        if (ContainsIgnoreCase(name, "新闻")) {
            return kPresets[0].url;
        }
        if (ContainsIgnoreCase(name, "交通")) {
            return kPresets[10].url;
        }
        if (ContainsIgnoreCase(name, "天气")) {
            return kPresets[11].url;
        }
        return "";
    }

    std::string ListPresetStations(const std::string& name) {
        std::string result;
        int count = 0;
        for (const auto& preset : kPresets) {
            if (!name.empty() && !ContainsIgnoreCase(preset.name, name) &&
                !ContainsIgnoreCase(preset.keywords, name)) {
                continue;
            }
            if (result.empty()) {
                result = "内置中文 MP3 电台：";
            }
            result += "\n" + std::to_string(++count) + ". " + preset.name;
        }
        if (!result.empty()) {
            result += "\n可按音乐、新闻、交通、天气分类播放，也可以直接说电台名称。";
        }
        return result;
    }

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

    bool StreamMp3(Http& http, uint32_t stream_id) {
        static std::once_flag decoder_registration;
        static esp_audio_err_t registration_result = ESP_AUDIO_ERR_OK;
        std::call_once(decoder_registration, []() {
            registration_result = esp_mp3_dec_register();
        });
        if (registration_result != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "MP3 decoder registration failed: %d", registration_result);
            return false;
        }

        esp_audio_simple_dec_cfg_t decoder_config = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
            .dec_cfg = nullptr,
            .cfg_size = 0,
            .use_frame_dec = false,
        };
        esp_audio_simple_dec_handle_t decoder = nullptr;
        auto result = esp_audio_simple_dec_open(&decoder_config, &decoder);
        if (result != ESP_AUDIO_ERR_OK || decoder == nullptr) {
            ESP_LOGE(TAG, "MP3 decoder open failed: %d", result);
            return false;
        }

        auto& audio_service = Application::GetInstance().GetAudioService();
        const int output_rate = Board::GetInstance().GetAudioCodec()->output_sample_rate();
        esp_ae_rate_cvt_handle_t resampler = nullptr;
        int resampler_source_rate = 0;
        bool pcm_started = false;
        std::vector<uint8_t> input(2048);
        std::vector<uint8_t> output(8192);
        uint32_t read_count = 0;
        uint32_t decode_count = 0;
        size_t input_total = 0;
        size_t pcm_total = 0;

        while (!ShouldStop(stream_id)) {
            int read = http.Read(reinterpret_cast<char*>(input.data()), input.size());
            ++read_count;
            if (read > 0) {
                input_total += read;
            }
            bool trace_read = read_count <= 3 || read_count % 500 == 0 || read <= 0;
            if (trace_read) {
                ESP_LOGI(TAG, "MP3-DIAG read#%lu bytes=%d input_total=%u stop=%d",
                         static_cast<unsigned long>(read_count), read, static_cast<unsigned>(input_total),
                         ShouldStop(stream_id));
            }
            if (read < 0) {
                ESP_LOGE(TAG, "MP3 stream read failed");
                break;
            }
            if (read == 0) {
                ESP_LOGI(TAG, "MP3 stream reached EOF");
                break;
            }

            esp_audio_simple_dec_raw_t raw = {
                .buffer = input.data(),
                .len = static_cast<uint32_t>(read),
                .eos = false,
                .consumed = 0,
                .frame_recover = ESP_AUDIO_SIMPLE_DEC_RECOVERY_NONE,
            };
            while (raw.len > 0 && !ShouldStop(stream_id)) {
                esp_audio_simple_dec_out_t frame = {
                    .buffer = output.data(),
                    .len = static_cast<uint32_t>(output.size()),
                    .needed_size = 0,
                    .decoded_size = 0,
                };
                result = esp_audio_simple_dec_process(decoder, &raw, &frame);
                ++decode_count;
                bool trace_decode = decode_count <= 3 || decode_count % 500 == 0 || result != ESP_AUDIO_ERR_OK;
                if (trace_decode) {
                    ESP_LOGI(TAG, "MP3-DIAG decode#%lu result=%d raw_len=%lu consumed=%lu decoded=%lu needed=%lu out_cap=%u",
                             static_cast<unsigned long>(decode_count), result, static_cast<unsigned long>(raw.len),
                             static_cast<unsigned long>(raw.consumed), static_cast<unsigned long>(frame.decoded_size),
                             static_cast<unsigned long>(frame.needed_size), static_cast<unsigned>(output.size()));
                }
                if (result == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && frame.needed_size > output.size()) {
                    output.resize(frame.needed_size);
                    continue;
                }
                if (result != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(TAG, "MP3 decode failed: %d", result);
                    raw.len = 0;
                    break;
                }

                if (frame.decoded_size > 0) {
                    esp_audio_simple_dec_info_t info = {};
                    if (esp_audio_simple_dec_get_info(decoder, &info) != ESP_AUDIO_ERR_OK ||
                        info.bits_per_sample != 16 || info.channel == 0) {
                        ESP_LOGE(TAG, "Unsupported MP3 PCM format");
                        raw.len = 0;
                        break;
                    }

                    const auto* samples = reinterpret_cast<const int16_t*>(frame.buffer);
                    size_t sample_count = frame.decoded_size / sizeof(int16_t);
                    size_t frame_count = sample_count / info.channel;
                    std::vector<int16_t> mono(frame_count);
                    for (size_t i = 0; i < frame_count; ++i) {
                        if (info.channel == 1) {
                            mono[i] = samples[i];
                        } else {
                            int32_t mixed = 0;
                            for (int channel = 0; channel < info.channel; ++channel) {
                                mixed += samples[i * info.channel + channel];
                            }
                            mono[i] = static_cast<int16_t>(mixed / info.channel);
                        }
                    }

                    if (!pcm_started) {
                        ESP_LOGI(TAG, "MP3 PCM ready: %lu Hz, %u channel, %u bit, %lu bps",
                                 static_cast<unsigned long>(info.sample_rate), info.channel,
                                 info.bits_per_sample, static_cast<unsigned long>(info.bitrate));
                        pcm_started = true;
                    }

                    if (static_cast<int>(info.sample_rate) != output_rate) {
                        size_t source_samples = mono.size();
                        if (resampler == nullptr || resampler_source_rate != static_cast<int>(info.sample_rate)) {
                            if (resampler != nullptr) {
                                esp_ae_rate_cvt_close(resampler);
                            }
                            esp_ae_rate_cvt_cfg_t rate_config = {
                                .src_rate = info.sample_rate,
                                .dest_rate = static_cast<uint32_t>(output_rate),
                                .channel = 1,
                                .bits_per_sample = 16,
                                .complexity = 1,
                                .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY,
                            };
                            if (esp_ae_rate_cvt_open(&rate_config, &resampler) != ESP_AE_ERR_OK || resampler == nullptr) {
                                ESP_LOGE(TAG, "MP3 resampler open failed: %lu -> %d",
                                         static_cast<unsigned long>(info.sample_rate), output_rate);
                                raw.len = 0;
                                break;
                            }
                            resampler_source_rate = info.sample_rate;
                        }
                        uint32_t output_samples = 0;
                        esp_ae_rate_cvt_get_max_out_sample_num(resampler, mono.size(), &output_samples);
                        std::vector<int16_t> converted(output_samples);
                        if (esp_ae_rate_cvt_process(resampler, mono.data(), mono.size(),
                                                    converted.data(), &output_samples) != ESP_AE_ERR_OK) {
                            ESP_LOGE(TAG, "MP3 resampling failed");
                            raw.len = 0;
                            break;
                        }
                        converted.resize(output_samples);
                        mono = std::move(converted);
                        if (trace_decode) {
                            ESP_LOGI(TAG, "MP3-DIAG resample src=%u@%lu dst=%u@%d",
                                     static_cast<unsigned>(source_samples), static_cast<unsigned long>(info.sample_rate),
                                     static_cast<unsigned>(mono.size()), output_rate);
                        }
                    }

                    size_t playback_samples = mono.size();
                    bool queued = audio_service.PushPcmToPlaybackQueue(std::move(mono), true);
                    pcm_total += playback_samples;
                    if (trace_decode || !queued) {
                        ESP_LOGI(TAG, "MP3-DIAG queue samples=%u pcm_total=%u queued=%d",
                                 static_cast<unsigned>(playback_samples), static_cast<unsigned>(pcm_total), queued);
                    }
                    if (!queued) {
                        raw.len = 0;
                        break;
                    }
                }

                if (raw.consumed == 0 || raw.consumed > raw.len) {
                    break;
                }
                raw.buffer += raw.consumed;
                raw.len -= raw.consumed;
            }
        }

        if (resampler != nullptr) {
            esp_ae_rate_cvt_close(resampler);
        }
        esp_audio_simple_dec_close(decoder);
        ESP_LOGI(TAG, "MP3 stream stopped, pcm_started=%d reads=%lu decodes=%lu input=%u pcm=%u",
                 pcm_started, static_cast<unsigned long>(read_count), static_cast<unsigned long>(decode_count),
                 static_cast<unsigned>(input_total), static_cast<unsigned>(pcm_total));
        return pcm_started;
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

        if (Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking) {
            ESP_LOGI(TAG, "Waiting for assistant speech to finish before starting radio");
        }
        while (Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking && !ShouldStop(stream_id)) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (ShouldStop(stream_id)) {
            MarkStopped(stream_id);
            return;
        }

        uint32_t reconnect_count = 0;
        while (!ShouldStop(stream_id)) {
            struct ConnectionCandidate {
                std::string url;
                std::string host_header;
            };
            std::vector<ConnectionCandidate> candidates = {{url, ""}};
            if (url.rfind("https://", 0) == 0) {
                auto http_url = url;
                http_url.replace(0, strlen("https://"), "http://");
                candidates.push_back({std::move(http_url), ""});
            }
            static constexpr const char* kQtPrimaryHost = "lhttp.qtfm.cn";
            auto primary_host_pos = url.find(kQtPrimaryHost);
            if (primary_host_pos != std::string::npos) {
                auto backup_url = url;
                backup_url.replace(primary_host_pos, strlen(kQtPrimaryHost), "lhttp.qingting.fm");
                candidates.push_back({std::move(backup_url), ""});

                auto ip_url = url;
                ip_url.replace(0, primary_host_pos + strlen(kQtPrimaryHost), "http://123.60.16.94");
                candidates.push_back({std::move(ip_url), kQtPrimaryHost});
            }

            std::unique_ptr<Http> http;
            std::string current_url;
            int status_code = -1;
            bool connected = false;
            for (size_t attempt = 0; attempt < candidates.size() && !ShouldStop(stream_id);
                 ++attempt) {
                current_url = candidates[attempt].url;
                ESP_LOGI(TAG, "Radio connection attempt %u/%u: %s",
                         static_cast<unsigned>(attempt + 1),
                         static_cast<unsigned>(candidates.size()), current_url.c_str());
                for (int redirect = 0; redirect < 4 && !ShouldStop(stream_id); ++redirect) {
                    http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                    http->SetTimeout(20000);
                    http->SetHeader("Accept",
                                    "audio/mpeg, audio/ogg, application/ogg, audio/opus, */*");
                    http->SetHeader("User-Agent", "XiaoZhiWave-Radio/1.0");
                    if (!candidates[attempt].host_header.empty()) {
                        http->SetHeader("Host", candidates[attempt].host_header);
                    }
                    if (!http->Open("GET", current_url)) {
                        ESP_LOGW(TAG, "Radio connection attempt %u failed, error=%d",
                                 static_cast<unsigned>(attempt + 1), http->GetLastError());
                        break;
                    }

                    status_code = http->GetStatusCode();
                    if (status_code >= 300 && status_code < 400) {
                        auto location = http->GetResponseHeader("Location");
                        http->Close();
                        if (location.rfind("http://", 0) != 0 &&
                            location.rfind("https://", 0) != 0) {
                            location.clear();
                        }
                        if (location.empty()) {
                            break;
                        }
                        ESP_LOGI(TAG, "Radio redirect %d: %s", status_code, location.c_str());
                        current_url = location;
                        continue;
                    }
                    connected = status_code >= 200 && status_code < 300;
                    break;
                }
                if (connected) {
                    break;
                }
            }

            if (!http || !connected || ShouldStop(stream_id)) {
                if (http) {
                    http->Close();
                }
                if (!ShouldStop(stream_id)) {
                    ShowError(stream_id, status_code < 0 ? "电台连接超时" : "电台响应异常");
                }
                MarkStopped(stream_id);
                return;
            }

            auto content_type = http->GetResponseHeader("Content-Type");
            bool is_mp3 = current_url.find(".mp3") != std::string::npos ||
                          content_type.find("audio/mpeg") != std::string::npos ||
                          content_type.find("audio/mp3") != std::string::npos;
            ESP_LOGI(TAG, "Radio stream opened: status=%d, type=%s, decoder=%s", status_code,
                     content_type.c_str(), is_mp3 ? "MP3" : "Ogg Opus");
            if (is_mp3) {
                bool played = StreamMp3(*http, stream_id);
                http->Close();
                if (ShouldStop(stream_id)) {
                    MarkStopped(stream_id);
                    return;
                }
                if (!played) {
                    ShowError(stream_id, "MP3解码失败");
                    MarkStopped(stream_id);
                    return;
                }
                ESP_LOGW(TAG, "MP3 stream ended; reconnecting radio, count=%lu",
                         static_cast<unsigned long>(++reconnect_count));
                for (int wait = 0; wait < 10 && !ShouldStop(stream_id); ++wait) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                continue;
            }

            auto& audio_service = Application::GetInstance().GetAudioService();
            auto demuxer = std::make_unique<OggDemuxer>();
            demuxer->OnDemuxerFinished([this, &audio_service, stream_id](
                                           const uint8_t* data, int sample_rate, size_t size) {
                if (ShouldStop(stream_id)) {
                    return;
                }
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
                    if (!ShouldStop(stream_id)) {
                        ESP_LOGW(TAG, "Ogg stream read failed; reconnecting");
                    }
                    break;
                }
                if (ret == 0) {
                    ESP_LOGW(TAG, "Ogg stream reached EOF");
                    break;
                }
                if (ShouldStop(stream_id)) {
                    break;
                }
                demuxer->Process(reinterpret_cast<const uint8_t*>(buffer), ret);
            }

            http->Close();
            if (!ShouldStop(stream_id)) {
                ESP_LOGW(TAG, "Ogg stream ended; reconnecting radio, count=%lu",
                         static_cast<unsigned long>(++reconnect_count));
                for (int wait = 0; wait < 10 && !ShouldStop(stream_id); ++wait) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        }
        MarkStopped(stream_id);
    }

    void ShowError(uint32_t stream_id, const char* message) {
        if (ShouldStop(stream_id)) {
            return;
        }
        auto display = Board::GetInstance().GetDisplay();
        display->SetEmotion("confused");
        display->SetChatMessage("system", message);
        display->ShowNotification(message, 3000);
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateListening) {
                ESP_LOGI(TAG, "Radio failed; returning from paused listening state to idle");
                app.SetDeviceState(kDeviceStateIdle);
            }
        });
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

bool IsVoiceTimerRunning() {
    return VoiceTimerManager::GetInstance().IsRunning();
}

void CancelVoiceTimer() {
    VoiceTimerManager::GetInstance().Cancel();
}

bool IsRadioPlaying() {
    return RadioStreamManager::GetInstance().IsRunning();
}

void StopRadioPlayback(bool update_display) {
    if (RadioStreamManager::GetInstance().IsRunning()) {
        ESP_LOGI(TAG, "Stopping radio for user interaction");
        RadioStreamManager::GetInstance().Stop(update_display);
    }
}

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
        "Only use the built-in stations; never search the web or invent a station URL. "
        "The four built-in categories are 音乐, 新闻, 交通, and 天气. Pass the requested category or exact station name in name. "
        "The firmware directly plays MP3 and Ogg Opus streams; AAC/AAC+ is not supported yet.",
        PropertyList({
            Property("name", kPropertyTypeString, std::string("网络收音机"))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto name = properties["name"].value<std::string>();
            return RadioStreamManager::GetInstance().Start("", name);
        });

    AddTool("self.radio.list",
        "List playable internet radio stations. Use this when the user asks what stations are available, 有哪些电台, 推荐几个电台, "
        "or wants to test radio playback but does not know station names. This only returns built-in stations and never searches the web.",
        PropertyList({
            Property("name", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto name = properties["name"].value<std::string>();
            return RadioStreamManager::GetInstance().List(name, "", "", "");
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
