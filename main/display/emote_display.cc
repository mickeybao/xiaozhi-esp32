#include "emote_display.h"

// Standard C++ headers
#include <cstring>
#include <memory>
#include <unordered_map>
#include <tuple>
#include <algorithm>
#include <cinttypes>

// Standard C headers
#include <sys/time.h>
#include <time.h>

// ESP-IDF headers
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_timer.h>
#include <lvgl.h>

// FreeRTOS headers
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Project headers
#include "assets/lang_config.h"
#include "assets.h"
#include "board.h"
#include "gfx.h"
#include "expression_emote.h"
#include "application.h"
#include "mcp_server.h"
#include "font_awesome.h"


namespace emote {

// ============================================================================
// Constants and Type Definitions
// ============================================================================

static const char* TAG = "EmoteDisplay";

// ============================================================================
// Forward Declarations
// ============================================================================

class EmoteDisplay;

// ============================================================================
// Helper Functions
// ============================================================================

static bool OnFlushIoReady(const esp_lcd_panel_io_handle_t panel_io,
    esp_lcd_panel_io_event_data_t* const edata, void* user_ctx)
{
    emote_handle_t handle = static_cast<emote_handle_t>(user_ctx);
    if (handle) {
        emote_notify_flush_finished(handle);
    }
    return true;
}

// Flush callback for emote
static void OnFlushCallback(int x_start, int y_start, int x_end, int y_end, const void* data, emote_handle_t handle)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)emote_get_user_data(handle);
    if (panel != nullptr) {
        esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, data);
    }
}

static bool SameIcon(const char* icon, const char* expected)
{
    return icon != nullptr && expected != nullptr && strcmp(icon, expected) == 0;
}

static bool IsWifiIcon(const char* icon)
{
    return SameIcon(icon, FONT_AWESOME_WIFI) ||
        SameIcon(icon, FONT_AWESOME_WIFI_FAIR) ||
        SameIcon(icon, FONT_AWESOME_WIFI_WEAK) ||
        SameIcon(icon, FONT_AWESOME_WIFI_SLASH);
}

static const char* GetNetworkIconAssetName(const char* icon)
{
    if (IsWifiIcon(icon)) {
        if (SameIcon(icon, FONT_AWESOME_WIFI_SLASH)) {
            return "icon_wifi_off";
        }
        if (SameIcon(icon, FONT_AWESOME_WIFI_WEAK)) {
            return "icon_wifi_weak";
        }
        if (SameIcon(icon, FONT_AWESOME_WIFI_FAIR)) {
            return "icon_wifi_fair";
        }
        return "icon_wifi_strong";
    }

    if (SameIcon(icon, FONT_AWESOME_SIGNAL_OFF)) {
        return "icon_4g_off";
    }
    if (SameIcon(icon, FONT_AWESOME_SIGNAL_WEAK)) {
        return "icon_4g_weak";
    }
    if (SameIcon(icon, FONT_AWESOME_SIGNAL_FAIR)) {
        return "icon_4g_fair";
    }
    if (SameIcon(icon, FONT_AWESOME_SIGNAL_GOOD)) {
        return "icon_4g_good";
    }
    return "icon_4g_strong";
}

// ============================================================================
// Graphics Initialization Functions
// ============================================================================

static emote_handle_t InitializeEmote(const esp_lcd_panel_handle_t panel, const int width, const int height)
{
    if (!panel) {
        ESP_LOGE(TAG, "Invalid panel");
        return nullptr;
    }

    emote_config_t emote_cfg = {
        .flags = {
            .swap = true,
            .double_buffer = true,
            .buff_dma = false,
        },
        .gfx_emote = {
            .h_res = width,
            .v_res = height,
            .fps = 30,
        },
        .buffers = {
            .buf_pixels = static_cast<size_t>(width * 16),
        },
        .task = {
            .task_priority = 5,
            .task_stack = 6 * 1024,
            .task_affinity = 0,
            .task_stack_in_ext = false,
        },
        .flush_cb = OnFlushCallback,
        .user_data = (void*)panel,
    };

    emote_handle_t emote_handle = emote_init(&emote_cfg);
    if (!emote_handle) {
        ESP_LOGE(TAG, "Failed to initialize emote");
        return nullptr;
    }

    return emote_handle;
}

// ============================================================================
// EmoteDisplay Class Implementation
// ============================================================================

EmoteDisplay::EmoteDisplay(const esp_lcd_panel_handle_t panel, const esp_lcd_panel_io_handle_t panel_io,
                           const int width, const int height)
{
    emote_handle_ = InitializeEmote(panel, width, height);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = OnFlushIoReady,
    };
    esp_lcd_panel_io_register_event_callbacks(panel_io, &cbs, emote_handle_);
}

EmoteDisplay::~EmoteDisplay()
{
    if (emote_handle_) {
        emote_deinit(emote_handle_);
        emote_handle_ = nullptr;
    }
}

void EmoteDisplay::SetEmotion(const char* const emotion)
{
    ESP_LOGI(TAG, "SetEmotion: %s", emotion);
    if (emote_handle_ && emotion && strlen(emotion) > 0) {
        emote_set_anim_emoji(emote_handle_, emotion);
    }
}

void EmoteDisplay::SetChatMessage(const char* const role, const char* const content)
{
    ESP_LOGI(TAG, "SetChatMessage: %s, %s", role, content);
    if (emote_handle_ && content && strlen(content) > 0) {
        if ((std::strcmp(role, "system") == 0) && std::strstr(content, "xiaozhi.me")) {
            size_t len = strlen(content);
            char* new_content = new char[len + 1];
            strcpy(new_content, content);
            std::replace(new_content, new_content + len, static_cast<char>(0x0A), static_cast<char>(0x20));
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, new_content);
            delete[] new_content;
        } else {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, content);
        }
    }
}

void EmoteDisplay::SetStatus(const char* const status)
{
    ESP_LOGI(TAG, "SetStatus: %s", status);
    if (emote_handle_ && status && strlen(status) > 0) {
        if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_LISTEN, NULL);
        } else if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_IDLE, NULL);
        } else if (std::strcmp(status, Lang::Strings::SPEAKING) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SPEAK, NULL);
        } else if (std::strcmp(status, Lang::Strings::ERROR) == 0) {
            emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SET, NULL);
        }
    }
}

void EmoteDisplay::ShowNotification(const char* notification, int duration_ms)
{
    ESP_LOGI(TAG, "ShowNotification: %s", notification);
    if (emote_handle_ && notification && strlen(notification) > 0) {
        emote_set_event_msg(emote_handle_, EMOTE_MGR_EVT_SYS, notification);
    }
}

void EmoteDisplay::UpdateStatusBar(bool update_all)
{
    ESP_LOGD(TAG, "UpdateStatusBar: %s", update_all ? "true" : "false");
    if (!emote_handle_) {
        return;
    }

    auto& board = Board::GetInstance();

    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        char battery_message[16];
        snprintf(battery_message, sizeof(battery_message), "%d%%", battery_level);
        battery_text_ = battery_message;
        emote_lock(emote_handle_);
        if (auto battery_label = emote_get_obj_by_name(emote_handle_, "battery_label")) {
            gfx_label_set_text(battery_label, battery_text_.c_str());
            gfx_obj_set_visible(battery_label, true);
        }
        emote_unlock(emote_handle_);
    }

    auto state = Application::GetInstance().GetDeviceState();
    if (!IsRadioPlaying() &&
        (state == kDeviceStateIdle || state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring ||
         state == kDeviceStateActivating)) {
        const char* network_icon = board.GetNetworkStateIcon();
        const char* icon_name = GetNetworkIconAssetName(network_icon);
        network_text_ = IsWifiIcon(network_icon) ? "WiFi" : "4G";
        icon_data_t* icon = nullptr;
        if (emote_get_icon_data_by_name(emote_handle_, icon_name, &icon) == ESP_OK && icon != nullptr && icon->data != nullptr) {
            emote_lock(emote_handle_);
            if (auto status_icon = emote_get_obj_by_name(emote_handle_, "status_icon")) {
                memcpy(&network_icon_dsc_.header, icon->data, sizeof(gfx_image_header_t));
                network_icon_dsc_.data = static_cast<const uint8_t*>(icon->data) + sizeof(gfx_image_header_t);
                network_icon_dsc_.data_size = icon->size - sizeof(gfx_image_header_t);
                gfx_img_set_src(status_icon, &network_icon_dsc_);
                gfx_obj_set_visible(status_icon, true);
            }
            if (auto network_label = emote_get_obj_by_name(emote_handle_, "network_label")) {
                gfx_label_set_text(network_label, network_text_.c_str());
                gfx_obj_set_visible(network_label, true);
            }
            emote_unlock(emote_handle_);
            RefreshAll();
        }
    }
}

void EmoteDisplay::SetPowerSaveMode(bool on)
{
    ESP_LOGI(TAG, "SetPowerSaveMode: %s", on ? "ON" : "OFF");
    if (!emote_handle_) {
        return;
    }
}

void EmoteDisplay::SetPreviewImage(const void* image)
{
    if (image) {
        ESP_LOGI(TAG, "SetPreviewImage: Preview image not supported, using default icon");
    }
}

void EmoteDisplay::SetTheme(Theme* const theme)
{
    ESP_LOGI(TAG, "SetTheme: %p", theme);
}

bool EmoteDisplay::Lock(const int timeout_ms)
{
    (void)timeout_ms;
    return true;
}

void EmoteDisplay::Unlock()
{
}

bool EmoteDisplay::StopAnimDialog()
{
    ESP_LOGI(TAG, "StopAnimDialog");
    if (emote_handle_) {
        return emote_stop_anim_dialog(emote_handle_);
    }
    return false;
}

bool EmoteDisplay::InsertAnimDialog(const char* emoji_name, uint32_t duration_ms)
{
    ESP_LOGI(TAG, "InsertAnimDialog: %s, %" PRIu32, emoji_name, duration_ms);
    if (emote_handle_ && emoji_name) {
        return emote_insert_anim_dialog(emote_handle_, emoji_name, duration_ms);
    }
    return false;
}

void EmoteDisplay::RefreshAll()
{
    if (emote_handle_) {
        emote_notify_all_refresh(emote_handle_);
        return;
    }
}

} // namespace emote
