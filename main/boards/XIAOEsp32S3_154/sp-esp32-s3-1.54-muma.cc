#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "assets/lang_config.h"
#include "power_save_timer.h"

#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>

#define TAG "SeeedXiaoS3St7789"

class SeeedXiaoS3St7789Board : public WifiBoard {
private:
    Button boot_button_;
    Display* display_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    PowerSaveTimer* power_save_timer_ = nullptr;

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;     // GPIO8  / D9  -> TFT SDA
        buscfg.miso_io_num = GPIO_NUM_NC;              // TFT SDO 悬空
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;     // GPIO9  / D10 -> TFT SCL
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);

        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;    // GPIO43 / D6 -> TFT CS
        io_config.dc_gpio_num = DISPLAY_SPI_DC_PIN;    // GPIO44 / D7 -> TFT DC
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;   // GPIO7 / D8 -> TFT RST
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;

        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        panel_ = panel;
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        // ST7789 常见模块一般需要反色；如果颜色异常，可改成 false 测试
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));

        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new SpiLcdDisplay(
            panel_io,
            panel,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X,
            DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y,
            DISPLAY_SWAP_XY
        );
    }


    void InitializePowerSaveTimer() {
        // 只息屏，不进入 deep sleep；麦克风和语音唤醒继续工作
        // 参数含义：button_gpio=-1 表示没有唤醒按键，120 秒无操作息屏，0 表示不自动关机/深睡
        power_save_timer_ = new PowerSaveTimer(-1, 120, 0);

        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "LCD enter power save mode");

            if (display_ != nullptr) {
                display_->SetPowerSaveMode(true);
            }

            if (panel_ != nullptr) {
                esp_lcd_panel_disp_on_off(panel_, false);
            }
        });

        power_save_timer_->OnExitSleepMode([this]() {
            ESP_LOGI(TAG, "LCD exit power save mode");

            if (panel_ != nullptr) {
                esp_lcd_panel_disp_on_off(panel_, true);
            }

            if (display_ != nullptr) {
                display_->SetPowerSaveMode(false);
            }
        });

        power_save_timer_->SetEnabled(true);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();

            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }

            app.ToggleChatState();
        });
    }

public:
    SeeedXiaoS3St7789Board() : boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Init Seeed XIAO ESP32S3 + ST7789 + MAX98357 + INMP441");

        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        InitializePowerSaveTimer();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

   /* virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        // 分离 I2S：喇叭和麦克风使用不同 WS/BCLK
        // MAX98357:
        //   BCLK -> GPIO2
        //   LRC  -> GPIO1
        //   DIN  -> GPIO3
        // INMP441:
        //   SCK  -> GPIO5
        //   WS   -> GPIO4
        //   SD   -> GPIO6
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK,
            AUDIO_I2S_SPK_GPIO_WS,
            AUDIO_I2S_SPK_GPIO_DOUT,
            AUDIO_I2S_MIC_GPIO_BCLK,
            AUDIO_I2S_MIC_GPIO_WS,
            AUDIO_I2S_MIC_GPIO_DIN
        );
#else
        // 共用 I2S：麦克风必须和喇叭共用 BCLK/WS
        // 也就是 INMP441 SCK 接 GPIO2，WS 接 GPIO1，SD 接 GPIO6
        static NoAudioCodecDuplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN
        );
#endif

        return &audio_codec;
    }*/

    virtual AudioCodec* GetAudioCodec() override {
    static NoAudioCodecSimplex audio_codec(
        AUDIO_INPUT_SAMPLE_RATE,
        AUDIO_OUTPUT_SAMPLE_RATE,

        // Speaker: MAX98357
        AUDIO_I2S_SPK_GPIO_BCLK,   // GPIO2  -> BCLK
        AUDIO_I2S_SPK_GPIO_LRCK,   // GPIO1  -> LRC
        AUDIO_I2S_SPK_GPIO_DOUT,   // GPIO3  -> DIN

        // Microphone: INMP441
        AUDIO_I2S_MIC_GPIO_SCK,    // GPIO5  -> SCK
        AUDIO_I2S_MIC_GPIO_WS,     // GPIO4  -> WS
        AUDIO_I2S_MIC_GPIO_DIN     // GPIO6  -> SD
    );

    return &audio_codec;
}

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        // 设备进入/退出低功耗等级时，同步唤醒 LCD
        if (level != PowerSaveLevel::LOW_POWER && power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }

        WifiBoard::SetPowerSaveLevel(level);
    }
};

DECLARE_BOARD(SeeedXiaoS3St7789Board);