#include "dual_network_board.h"
#include "display/lcd_display.h"
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

class WaveshareEsp32s3TouchLCD2inch : public DualNetworkBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    Display* display_;
    PowerSaveTimer* power_save_timer_;
    AdcBatteryMonitor* battery_monitor_;
    std::unique_ptr<Qmi8658> imu_;
    TaskHandle_t posture_task_handle_ = nullptr;
    TaskHandle_t countdown_task_handle_ = nullptr;
    bool countdown_armed_ = true;
    int normal_z_sign_ = 0;

    static const std::string_view& DigitSound(int digit) {
        static const std::array<std::reference_wrapper<const std::string_view>, 10> sounds = {
            std::cref(Lang::Sounds::OGG_0),
            std::cref(Lang::Sounds::OGG_1),
            std::cref(Lang::Sounds::OGG_2),
            std::cref(Lang::Sounds::OGG_3),
            std::cref(Lang::Sounds::OGG_4),
            std::cref(Lang::Sounds::OGG_5),
            std::cref(Lang::Sounds::OGG_6),
            std::cref(Lang::Sounds::OGG_7),
            std::cref(Lang::Sounds::OGG_8),
            std::cref(Lang::Sounds::OGG_9),
        };
        return sounds[digit].get();
    }

    bool IsCountdownRunning() const {
        return countdown_task_handle_ != nullptr;
    }

    void PlayCountdownNumber(int number) {
        auto& app = Application::GetInstance();
        if (number == 10) {
            app.PlaySound(Lang::Sounds::OGG_1);
            vTaskDelay(pdMS_TO_TICKS(180));
            app.PlaySound(Lang::Sounds::OGG_0);
            return;
        }
        if (number >= 0 && number <= 9) {
            app.PlaySound(DigitSound(number));
        }
    }

    void StartFlipCountdown() {
        if (IsCountdownRunning()) {
            return;
        }

        xTaskCreate([](void* arg) {
            auto* self = static_cast<WaveshareEsp32s3TouchLCD2inch*>(arg);
            auto& app = Application::GetInstance();
            ESP_LOGI(TAG, "Starting flip countdown");

            app.Schedule([&app]() {
                auto state = app.GetDeviceState();
                if (state == kDeviceStateSpeaking || state == kDeviceStateListening || state == kDeviceStateConnecting) {
                    app.AbortSpeaking(kAbortReasonNone);
                    app.SetDeviceState(kDeviceStateIdle);
                }
                app.GetAudioService().ResetDecoder();
                auto display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->SetStatus("倒计时");
                    display->SetChatMessage("system", "10");
                }
            });

            vTaskDelay(pdMS_TO_TICKS(200));
            for (int i = 10; i >= 1; --i) {
                self->PlayCountdownNumber(i);
                app.Schedule([i]() {
                    auto display = Board::GetInstance().GetDisplay();
                    if (display != nullptr) {
                        char buffer[8];
                        snprintf(buffer, sizeof(buffer), "%d", i);
                        display->SetChatMessage("system", buffer);
                    }
                });
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            app.Schedule([]() {
                auto display = Board::GetInstance().GetDisplay();
                if (display != nullptr) {
                    display->SetChatMessage("system", "");
                }
            });

            ESP_LOGI(TAG, "Flip countdown finished");
            self->countdown_task_handle_ = nullptr;
            vTaskDelete(nullptr);
        }, "flip_countdown", 4096, this, 3, &countdown_task_handle_);
    }

    void InitializeBatteryMonitor() {
        battery_monitor_ = new AdcBatteryMonitor(
            BATTERY_ADC_UNIT,
            BATTERY_ADC_CHANNEL,
            BATTERY_UPPER_RESISTOR,
            BATTERY_LOWER_RESISTOR);
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
            int flipped_samples = 0;
            int restored_samples = 0;

            while (true) {
                Qmi8658::AccelData accel;
                if (!self->imu_->ReadAccel(accel)) {
                    vTaskDelay(pdMS_TO_TICKS(50));
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
                    vTaskDelay(pdMS_TO_TICKS(50));
                    continue;
                }

                bool flipped = self->normal_z_sign_ > 0 ? accel.z < -0.75f : accel.z > 0.75f;
                bool restored = self->normal_z_sign_ > 0 ? accel.z > 0.55f : accel.z < -0.55f;

                if (flipped) {
                    flipped_samples++;
                    restored_samples = 0;
                } else if (restored) {
                    restored_samples++;
                    flipped_samples = 0;
                } else {
                    flipped_samples = std::max(0, flipped_samples - 1);
                    restored_samples = std::max(0, restored_samples - 1);
                }

                if (self->countdown_armed_ && flipped_samples >= 24) {
                    auto state = Application::GetInstance().GetDeviceState();
                    if (state != kDeviceStateStarting && state != kDeviceStateActivating &&
                        state != kDeviceStateWifiConfiguring && state != kDeviceStateUpgrading) {
                        self->countdown_armed_ = false;
                        self->StartFlipCountdown();
                    }
                }

                if (!self->countdown_armed_ && restored_samples >= 20) {
                    ESP_LOGI(TAG, "Flip countdown re-armed");
                    self->countdown_armed_ = true;
                }

                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }, "posture", 4096, this, 3, &posture_task_handle_);
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

        // 液晶屏控制IO初始化
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
        // esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
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
        static NoAudioCodecSimplex audio_codec(
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
