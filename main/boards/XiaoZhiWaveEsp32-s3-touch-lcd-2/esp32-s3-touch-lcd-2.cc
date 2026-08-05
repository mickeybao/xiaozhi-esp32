#include "dual_network_board.h"
#include "display/lcd_display.h"
#include "display/emote_display.h"
#include "audio/codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "config.h"
#include "power_save_timer.h"
#include "adc_battery_monitor.h"
#include "i2c_device.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include "settings.h"

#include <esp_lcd_touch_cst816s.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <functional>
#include <cmath>
#include <memory>
#include <string_view>

#define TAG "WaveshareEsp32s3TouchLCD2inch"

class Qmi8658 : public I2cDevice {
public:
    struct AccelData {
        float x;
        float y;
        float z;
    };

    Qmi8658(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr), address_(addr) {}

    bool Initialize() {
        uint8_t who_am_i = 0;
        if (!TryReadReg(QMI8658_REG_WHO_AM_I, who_am_i)) {
            ESP_LOGW(TAG, "QMI8658 does not respond at 0x%02x", address_);
            return false;
        }
        if (who_am_i != QMI8658_WHO_AM_I_VALUE) {
            ESP_LOGW(TAG, "QMI8658 not found at 0x%02x, who_am_i=0x%02x", address_, who_am_i);
            return false;
        }

        if (!TryWriteReg(QMI8658_REG_RESET, QMI8658_RESET_CMD)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!TryWriteReg(QMI8658_REG_CTRL1, QMI8658_CTRL1_AUTO_INCREMENT) ||
            !TryWriteReg(QMI8658_REG_CTRL2, QMI8658_ACCEL_RANGE_4G | QMI8658_ACCEL_ODR_62_5HZ) ||
            !TryWriteReg(QMI8658_REG_CTRL7, QMI8658_CTRL7_ACC_ENABLE)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));

        ESP_LOGI(TAG, "QMI8658 initialized at 0x%02x", address_);
        return true;
    }

    bool ReadAccel(AccelData& data) {
        uint8_t status = 0;
        if (!TryReadReg(QMI8658_REG_STATUS0, status)) {
            return false;
        }
        if ((status & QMI8658_STATUS0_ACC_READY) == 0) {
            return false;
        }

        uint8_t buffer[6] = {};
        if (!TryReadRegs(QMI8658_REG_AX_L, buffer, sizeof(buffer))) {
            return false;
        }
        int16_t raw_x = static_cast<int16_t>((buffer[1] << 8) | buffer[0]);
        int16_t raw_y = static_cast<int16_t>((buffer[3] << 8) | buffer[2]);
        int16_t raw_z = static_cast<int16_t>((buffer[5] << 8) | buffer[4]);

        data.x = raw_x * ACCEL_4G_SCALE;
        data.y = raw_y * ACCEL_4G_SCALE;
        data.z = raw_z * ACCEL_4G_SCALE;
        return true;
    }

private:
    static constexpr uint8_t QMI8658_REG_WHO_AM_I = 0x00;
    static constexpr uint8_t QMI8658_REG_CTRL1 = 0x02;
    static constexpr uint8_t QMI8658_REG_CTRL2 = 0x03;
    static constexpr uint8_t QMI8658_REG_CTRL7 = 0x08;
    static constexpr uint8_t QMI8658_REG_STATUS0 = 0x2E;
    static constexpr uint8_t QMI8658_REG_AX_L = 0x35;
    static constexpr uint8_t QMI8658_REG_RESET = 0x60;
    static constexpr uint8_t QMI8658_WHO_AM_I_VALUE = 0x05;
    static constexpr uint8_t QMI8658_RESET_CMD = 0xB0;
    static constexpr uint8_t QMI8658_CTRL1_AUTO_INCREMENT = 0x40;
    static constexpr uint8_t QMI8658_ACCEL_RANGE_4G = 0x10;
    static constexpr uint8_t QMI8658_ACCEL_ODR_62_5HZ = 0x07;
    static constexpr uint8_t QMI8658_CTRL7_ACC_ENABLE = 0x01;
    static constexpr uint8_t QMI8658_STATUS0_ACC_READY = 0x01;
    static constexpr float ACCEL_4G_SCALE = 4.0f / 32768.0f;

    uint8_t address_ = 0;
};

class MotionAwareSpiLcdDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetEmotionOffset(int16_t offset_x) {
        DisplayLockGuard lock(this);
        if (emoji_box_ != nullptr) {
            lv_obj_set_style_translate_x(emoji_box_, offset_x, 0);
        } else {
            if (emoji_image_ != nullptr) {
                lv_obj_set_style_translate_x(emoji_image_, offset_x, 0);
            }
            if (emoji_label_ != nullptr) {
                lv_obj_set_style_translate_x(emoji_label_, offset_x, 0);
            }
        }
    }
};

class MotionAwareEmoteDisplay : public emote::EmoteDisplay {
public:
    using emote::EmoteDisplay::EmoteDisplay;

    void SetEmotionOffset(int16_t offset_x) {
        auto handle = GetEmoteHandle();
        if (handle == nullptr) {
            return;
        }

        emote_lock(handle);
        if (auto eye_anim = emote_get_obj_by_name(handle, "eye_anim")) {
            gfx_obj_align(eye_anim, GFX_ALIGN_LEFT_MID, offset_x, 20);
        }
        if (auto emerg_dlg = emote_get_obj_by_name(handle, "emerg_dlg")) {
            gfx_obj_align(emerg_dlg, GFX_ALIGN_LEFT_MID, offset_x, 20);
        }
        emote_unlock(handle);
        RefreshAll();
    }
};

class MotionAwareNoAudioCodecSimplex : public NoAudioCodecSimplex {
public:
    using NoAudioCodecSimplex::NoAudioCodecSimplex;

    void SetMotionMuted(bool muted) {
        motion_muted_ = muted;
    }

    virtual void OutputData(std::vector<int16_t>& data) override {
        if (motion_muted_) {
            return;
        }
        NoAudioCodecSimplex::OutputData(data);
    }

private:
    std::atomic<bool> motion_muted_ = false;
};

class WaveshareEsp32s3TouchLCD2inch : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Display* display_;
    MotionAwareSpiLcdDisplay* motion_display_ = nullptr;
    MotionAwareEmoteDisplay* motion_emote_display_ = nullptr;
    PowerSaveTimer* power_save_timer_;
    AdcBatteryMonitor* battery_monitor_;
    std::unique_ptr<Qmi8658> imu_;
    TaskHandle_t posture_task_handle_ = nullptr;
    TaskHandle_t touch_task_handle_ = nullptr;
    esp_timer_handle_t motion_emotion_reset_timer_ = nullptr;
    int normal_z_sign_ = 0;
    std::atomic<bool> motion_sleeping_ = false;
    std::atomic<bool> face_down_muted_ = false;
    std::atomic<bool> tilt_left_active_ = false;
    std::atomic<bool> temporary_motion_emotion_ = false;

    static constexpr const char* kAwakeEmotion = "surprised";
    static constexpr const char* kSleepEmotion = "sleepy";
    static constexpr const char* kShakeEmotion = "confused";
    static constexpr const char* kDndEmotion = "relaxed";

    void SetMotionEmotionOffset(int16_t offset_x) {
        if (motion_display_ != nullptr) {
            motion_display_->SetEmotionOffset(offset_x);
        }
        if (motion_emote_display_ != nullptr) {
            motion_emote_display_->SetEmotionOffset(offset_x);
        }
    }

    static void MotionEmotionResetTimerCallback(void* arg) {
        auto* self = static_cast<WaveshareEsp32s3TouchLCD2inch*>(arg);
        if (self != nullptr) {
            self->temporary_motion_emotion_ = false;
            self->ApplyPostureVisual();
        }
    }


    void InitializeBatteryMonitor() {
        battery_monitor_ = new AdcBatteryMonitor(
            BATTERY_ADC_UNIT,
            BATTERY_ADC_CHANNEL,
            BATTERY_UPPER_RESISTOR,
            BATTERY_LOWER_RESISTOR,
            GPIO_NUM_NC,
            false);
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
            GetBacklight()->SetBrightness(20); });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness(); });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeMotionFeedbackTimer() {
        const esp_timer_create_args_t timer_args = {
            .callback = &WaveshareEsp32s3TouchLCD2inch::MotionEmotionResetTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "motion_emoji",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &motion_emotion_reset_timer_));
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = DISPLAY_TOUCH_SDA_PIN,
            .scl_io_num = DISPLAY_TOUCH_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializePostureDetector() {
        for (uint8_t addr : {static_cast<uint8_t>(0x6A), static_cast<uint8_t>(0x6B)}) {
            auto imu = std::make_unique<Qmi8658>(i2c_bus_, addr);
            if (imu->Initialize()) {
                imu_ = std::move(imu);
                break;
            }
        }

        if (!imu_) {
            ESP_LOGW(TAG, "QMI8658 posture detector disabled");
            return;
        }

        xTaskCreate([](void* arg) {
            auto* self = static_cast<WaveshareEsp32s3TouchLCD2inch*>(arg);
            int normal_samples = 0;
            int still_flat_samples = 0;
            int face_down_samples = 0;
            int face_up_samples = 0;
            Qmi8658::AccelData prev = {};
            bool has_prev = false;
            int64_t last_shake_ms = 0;
            int64_t last_wake_ms = 0;

            constexpr float kShakeDeltaThresholdG = 2.4f;
            constexpr int64_t kShakeCooldownMs = 2000;
            constexpr float kStillDeltaThresholdG = 0.08f;
            constexpr float kWakeDeltaThresholdG = 0.28f;
            constexpr float kFlatZThresholdG = 0.75f;
            constexpr float kTiltLeftThresholdG = -0.45f;
            constexpr int kStillFlatSamples = 75;
            constexpr int kFaceDownSamples = 8;
            constexpr int kFaceUpSamples = 8;

            while (true) {
                Qmi8658::AccelData accel;
                if (!self->imu_->ReadAccel(accel)) {
                    vTaskDelay(pdMS_TO_TICKS(80));
                    continue;
                }

                if (self->normal_z_sign_ == 0) {
                    if (std::fabs(accel.z) > 0.65f) {
                        normal_samples++;
                        if (normal_samples >= 20) {
                            self->normal_z_sign_ = accel.z > 0 ? 1 : -1;
                            ESP_LOGI(TAG, "Posture baseline set, z sign=%d, acc=(%.2f, %.2f, %.2f)",
                                     self->normal_z_sign_, accel.x, accel.y, accel.z);
                        }
                    } else {
                        normal_samples = 0;
                    }
                    prev = accel;
                    has_prev = true;
                    vTaskDelay(pdMS_TO_TICKS(80));
                    continue;
                }

                float delta = 0.0f;
                if (has_prev) {
                    delta = std::fabs(accel.x - prev.x) + std::fabs(accel.y - prev.y) + std::fabs(accel.z - prev.z);
                    int64_t now_ms = esp_timer_get_time() / 1000;
                    if (delta > kShakeDeltaThresholdG && (now_ms - last_shake_ms) > kShakeCooldownMs) {
                        last_shake_ms = now_ms;
                        self->ShowDizzyFeedback();
                    } else if (delta > kWakeDeltaThresholdG && (now_ms - last_wake_ms) > 1000) {
                        last_wake_ms = now_ms;
                        self->WakeFromMotion();
                    }
                }

                bool face_down = self->normal_z_sign_ > 0 ? accel.z < -kFlatZThresholdG : accel.z > kFlatZThresholdG;
                bool face_up = self->normal_z_sign_ > 0 ? accel.z > kFlatZThresholdG : accel.z < -kFlatZThresholdG;
                bool flat_and_still = face_up && delta < kStillDeltaThresholdG;
                bool tilt_left = accel.x < kTiltLeftThresholdG && std::fabs(accel.z) < 0.9f;

                if (face_down) {
                    face_down_samples++;
                    face_up_samples = 0;
                    still_flat_samples = 0;
                } else if (face_up) {
                    face_up_samples++;
                    face_down_samples = 0;
                    if (flat_and_still && !self->face_down_muted_ && !self->temporary_motion_emotion_) {
                        still_flat_samples++;
                    } else {
                        still_flat_samples = 0;
                    }
                } else {
                    face_down_samples = 0;
                    face_up_samples = 0;
                    still_flat_samples = 0;
                }

                if (face_down_samples >= kFaceDownSamples && !self->face_down_muted_) {
                    self->SetFaceDownMuted(true);
                }
                if (face_up_samples >= kFaceUpSamples && self->face_down_muted_) {
                    self->SetFaceDownMuted(false);
                    self->WakeFromMotion();
                }
                if (still_flat_samples >= kStillFlatSamples && !self->motion_sleeping_) {
                    self->EnterMotionSleep();
                }
                if (tilt_left != self->tilt_left_active_ && !self->face_down_muted_ && !self->temporary_motion_emotion_) {
                    self->tilt_left_active_ = tilt_left;
                    self->ApplyPostureVisual();
                }

                prev = accel;
                has_prev = true;
                vTaskDelay(pdMS_TO_TICKS(80));
            }
        }, "posture", 4096, this, 3, &posture_task_handle_);
    }

    void ApplyPostureVisual() {
        auto& app = Application::GetInstance();
        app.Schedule([this]() {
            if (display_ == nullptr) {
                return;
            }

            if (face_down_muted_) {
                display_->SetStatus("免打扰");
                display_->SetEmotion(kDndEmotion);
                display_->SetChatMessage("system", "屏幕朝下，已静音");
                SetMotionEmotionOffset(0);
                return;
            }

            if (motion_sleeping_) {
                display_->SetPowerSaveMode(true);
                display_->SetEmotion(kSleepEmotion);
                display_->SetChatMessage("system", "");
                GetBacklight()->SetBrightness(12);
                SetMotionEmotionOffset(0);
                return;
            }

            display_->SetPowerSaveMode(false);
            display_->SetEmotion(kAwakeEmotion);
            display_->SetChatMessage("system", "");
            SetMotionEmotionOffset(tilt_left_active_ ? -28 : 0);
        });
    }

    void WakeFromMotion() {
        motion_sleeping_ = false;
        power_save_timer_->WakeUp();
        auto& app = Application::GetInstance();
        app.Schedule([this]() {
            if (display_ != nullptr && !face_down_muted_) {
                display_->SetPowerSaveMode(false);
                display_->SetEmotion(kAwakeEmotion);
                display_->SetChatMessage("system", "");
                GetBacklight()->RestoreBrightness();
                SetMotionEmotionOffset(tilt_left_active_ ? -28 : 0);
            }
        });
    }

    void EnterMotionSleep() {
        auto state = Application::GetInstance().GetDeviceState();
        if (state != kDeviceStateIdle) {
            return;
        }
        ESP_LOGI(TAG, "Motion sleep: flat and still");
        motion_sleeping_ = true;
        ApplyPostureVisual();
    }

    void ShowDizzyFeedback() {
        if (face_down_muted_) {
            return;
        }
        ESP_LOGI(TAG, "Motion shake detected");
        motion_sleeping_ = false;
        temporary_motion_emotion_ = true;
        power_save_timer_->WakeUp();

        Application::GetInstance().Schedule([this]() {
            auto& app = Application::GetInstance();
            if (display_ != nullptr) {
                display_->SetPowerSaveMode(false);
                display_->SetEmotion(kShakeEmotion);
                display_->SetChatMessage("system", "别摇啦");
                GetBacklight()->RestoreBrightness();
                SetMotionEmotionOffset(0);
            }
            app.PlaySound(Lang::Sounds::OGG_POPUP);
        });

        if (motion_emotion_reset_timer_ != nullptr) {
            esp_timer_stop(motion_emotion_reset_timer_);
            esp_timer_start_once(motion_emotion_reset_timer_, 1800 * 1000);
        }
    }

    void SetFaceDownMuted(bool muted) {
        face_down_muted_ = muted;
        motion_sleeping_ = false;
        Application::GetInstance().Schedule([this, muted]() {
            auto& app = Application::GetInstance();
            auto* codec = static_cast<MotionAwareNoAudioCodecSimplex*>(GetAudioCodec());
            codec->SetMotionMuted(muted);

            if (muted) {
                app.GetAudioService().ResetDecoder();
                if (display_ != nullptr) {
                    display_->SetStatus("免打扰");
                    display_->SetEmotion(kDndEmotion);
                    display_->SetChatMessage("system", "屏幕朝下，已静音");
                }
                SetMotionEmotionOffset(0);
            } else {
                if (display_ != nullptr) {
                    display_->SetStatus(Lang::Strings::STANDBY);
                    display_->SetEmotion(kAwakeEmotion);
                    display_->SetChatMessage("system", "");
                    GetBacklight()->RestoreBrightness();
                }
                SetMotionEmotionOffset(tilt_left_active_ ? -28 : 0);
            }
        });
    }

    void StartTouchWakeTask(esp_lcd_touch_handle_t tp) {
        if (touch_task_handle_ != nullptr) {
            return;
        }

        xTaskCreate([](void* arg) {
            auto* touch = static_cast<esp_lcd_touch_handle_t>(arg);
            bool was_pressed = false;
            int64_t last_click_ms = 0;

            while (true) {
                esp_lcd_touch_read_data(touch);

                uint16_t x = 0;
                uint16_t y = 0;
                uint8_t point_count = 0;
                bool pressed = esp_lcd_touch_get_coordinates(touch, &x, &y, nullptr, &point_count, 1);

                if (pressed && point_count > 0 && !was_pressed) {
                    int64_t now_ms = esp_timer_get_time() / 1000;
                    if ((now_ms - last_click_ms) > 500) {
                        last_click_ms = now_ms;
                        ESP_LOGI(TAG, "Touch wake at x=%u, y=%u", x, y);
                        auto& app = Application::GetInstance();
                        app.Schedule([&app]() {
                            app.ToggleChatState();
                        });
                    }
                }
                was_pressed = pressed && point_count > 0;
                vTaskDelay(pdMS_TO_TICKS(40));
            }
        }, "touch_wake", 3072, tp, 3, &touch_task_handle_);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH*  DISPLAY_HEIGHT*  sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (GetNetworkType() == NetworkType::WIFI &&
                (app.GetDeviceState() == kDeviceStateStarting ||
                 app.GetDeviceState() == kDeviceStateWifiConfiguring)) {
                static_cast<WifiBoard&>(GetCurrentBoard()).EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        boot_button_.OnLongPress([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting ||
                app.GetDeviceState() == kDeviceStateWifiConfiguring ||
                app.GetDeviceState() == kDeviceStateIdle) {
                SwitchNetworkType();
            }
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制 IO 初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 24 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
#if CONFIG_USE_EMOTE_MESSAGE_STYLE
        motion_emote_display_ = new MotionAwareEmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        display_ = motion_emote_display_;
#else
        motion_display_ = new MotionAwareSpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = motion_display_;
#endif
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_TOUCH_MAX_X,
            .y_max = DISPLAY_TOUCH_MAX_Y,
            .rst_gpio_num = DISPLAY_TOUCH_RST_PIN,
            .int_gpio_num = DISPLAY_TOUCH_INT_PIN,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {
            .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
            .on_color_trans_done = 0,
            .user_ctx = 0,
            .control_phase_bytes = 1,
            .dc_bit_offset = 0,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 0,
            .flags =
            {
                .dc_low_on_data = 0,
                .disable_control_phase = 1,
            },
        };
        tp_io_config.scl_speed_hz = 400*  1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        if (touch_cfg.disp == nullptr) {
            ESP_LOGW(TAG, "No LVGL display is registered; using touch wake task");
            StartTouchWakeTask(tp);
            return;
        }
        lv_indev_t* touch_indev = lvgl_port_add_touch(&touch_cfg);
        ESP_ERROR_CHECK(touch_indev != nullptr ? ESP_OK : ESP_FAIL);
        lv_indev_add_event_cb(touch_indev, [](lv_event_t* event) {
            auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            ESP_LOGI(TAG, "Touch released at x=%ld, y=%ld",
                     static_cast<long>(point.x), static_cast<long>(point.y));

            // Run the application state transition in the main application task
            // instead of blocking the LVGL input task.
            auto& app = Application::GetInstance();
            app.Schedule([&app]() {
                app.ToggleChatState();
            });
        }, LV_EVENT_RELEASED, nullptr);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }

    // 初始化工具
    void InitializeTools() {
        auto &mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.system.reconfigure_wifi",
            "End this conversation and enter WiFi configuration mode.\n"
            "**CAUTION** You must ask the user to confirm this action.",
            PropertyList(), [this](const PropertyList& properties) {
                if (GetNetworkType() != NetworkType::WIFI) {
                    return false;
                }
                static_cast<WifiBoard&>(GetCurrentBoard()).EnterWifiConfigMode();
                return true;
            });
    }

public:
    WaveshareEsp32s3TouchLCD2inch()
        : DualNetworkBoard(ML307_TX_PIN, ML307_RX_PIN, ML307_DTR_PIN,
                           DEFAULT_4G_NETWORK, ML307_BAUD_RATE),
          boot_button_(BOOT_BUTTON_GPIO) {
        InitializePowerSaveTimer();
        InitializeMotionFeedbackTimer();
        InitializeBatteryMonitor();
        InitializeI2c();
        InitializePostureDetector();
        InitializeSpi();
        InitializeDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static MotionAwareNoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT,
            I2S_STD_SLOT_LEFT,
            AUDIO_I2S_MIC_GPIO_SCK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN,
            I2S_STD_SLOT_LEFT);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        level = battery_monitor_->GetBatteryLevel();
        charging = battery_monitor_->IsCharging();
        discharging = battery_monitor_->IsDischarging();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        DualNetworkBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(WaveshareEsp32s3TouchLCD2inch);
