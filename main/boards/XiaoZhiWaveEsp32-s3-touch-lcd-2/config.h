#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

#define AUDIO_INPUT_SAMPLE_RATE 24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_INPUT_REFERENCE false
#define AUDIO_I2S_METHOD_SIMPLEX

// External MAX98357, header pins 20/21/19
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_8
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_10

// External INMP441, header pins 27/28/26
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_2
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6

#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define DISPLAY_SPI_HOST        SPI2_HOST
#define DISPLAY_CS_PIN          GPIO_NUM_45
#define DISPLAY_MOSI_PIN        GPIO_NUM_38
#define DISPLAY_MISO_PIN        GPIO_NUM_NC
#define DISPLAY_CLK_PIN         GPIO_NUM_39
#define DISPLAY_DC_PIN          GPIO_NUM_42
#define DISPLAY_RST_PIN         GPIO_NUM_NC

#define DISPLAY_TOUCH_SDA_PIN   GPIO_NUM_48
#define DISPLAY_TOUCH_SCL_PIN   GPIO_NUM_47
#define DISPLAY_TOUCH_INT_PIN   GPIO_NUM_NC
#define DISPLAY_TOUCH_RST_PIN   GPIO_NUM_NC
#define DISPLAY_TOUCH_MAX_X     320
#define DISPLAY_TOUCH_MAX_Y     240

#define DISPLAY_WIDTH           240
#define DISPLAY_HEIGHT          320
#define DISPLAY_MIRROR_X        false
#define DISPLAY_MIRROR_Y        false
#define DISPLAY_SWAP_XY         false

#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0

#define DISPLAY_BACKLIGHT_PIN   GPIO_NUM_1
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// Header pin 3 (TXD/GPIO43) -> ML307R RXD
// Header pin 4 (RXD/GPIO44) <- ML307R TXD
#define ML307_TX_PIN            GPIO_NUM_43
#define ML307_RX_PIN            GPIO_NUM_44
#define ML307_DTR_PIN           GPIO_NUM_NC
#define ML307_BAUD_RATE         115200
#define DEFAULT_4G_NETWORK      1

#endif // _BOARD_CONFIG_H_
