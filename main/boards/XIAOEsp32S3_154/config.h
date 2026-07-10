#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// Seeed XIAO ESP32-S3 + ST7789 1.54 TFT + MAX98357 + INMP441

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// =========================
// Audio: MAX98357 speaker amp + INMP441 microphone
// =========================
// MAX98357:
//   LRC  -> D0 / GPIO1
//   BCLK -> D1 / GPIO2
//   DIN  -> D2 / GPIO3
// INMP441:
//   WS   -> D3 / GPIO4
//   SCK  -> D4 / GPIO5
//   SD   -> D5 / GPIO6
//   L/R  -> GND
//
// NOTE:
// xiaozhi-esp32 common audio config only has one AUDIO_I2S_GPIO_WS/BCLK pair.
// If your board.cc uses a single NoAudioCodec/BoxAudioCodec constructor with
// AUDIO_I2S_GPIO_BCLK and AUDIO_I2S_GPIO_WS, then INMP441 must share the same
// BCLK/WS pins as MAX98357, or board.cc must be changed to use separate RX/TX
// I2S channels.

#define AUDIO_I2S_METHOD_SIMPLEX

// MAX98357
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_1   // D0 -> LRC
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_2   // D1 -> BCLK
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_3   // D2 -> DIN

// INMP441
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4   // D3 -> WS
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5   // D4 -> SCK
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6   // D5 -> SD

// 兼容旧宏，保留也没问题
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_1
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_2
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_3
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_NC


#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_NC

/*
// Default/common xiaozhi audio macros: mapped to speaker clock pair
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_1   // D0 -> MAX98357 LRC (I2S标准 WS / LRCLK 左右声道同步)
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_2   // D1 -> MAX98357 BCLK (I2S标准 位时钟) 
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_3   // D2 -> MAX98357 DIN  (I2S标准 功放输入数据)
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6   // D5 <- INMP441 SD (I2S标准 麦克风输出数据)

// Extra macros for your actual separated microphone clock wiring.
// These will only take effect if board.cc is modified to use them.//暂未生效
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4   // D3 -> INMP441 WS
#define AUDIO_I2S_MIC_GPIO_BCLK GPIO_NUM_5   // D4 -> INMP441 SCK
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6   // D5 <- INMP441 SD

#define AUDIO_I2S_SPK_GPIO_WS   GPIO_NUM_1   // D0 -> MAX98357 LRC
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_2   // D1 -> MAX98357 BCLK
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_3   // D2 -> MAX98357 DIN
*/
// No ES8311/ES7210 codec or I2C codec is used in this wiring.
//#define AUDIO_CODEC_PA_PIN       GPIO_NUM_NC
//#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_NC
//#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_NC
// Keep this macro only if your board source still references it.
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

// =========================
// Display: ST7789 240x240 SPI
// =========================
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0

// BL is connected directly to VCC, so there is no GPIO backlight control.
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define DISPLAY_SPI_SCLK_PIN    GPIO_NUM_9     //GPIO_NUM_9    // D10 -> TFT SCL/SCK
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_8      //GPIO_NUM_8    // D9  -> TFT SDA/MOSI
#define DISPLAY_SPI_CS_PIN      GPIO_NUM_43      //GPIO_NUM_43   // D6  -> TFT CS
#define DISPLAY_SPI_DC_PIN      GPIO_NUM_44      //GPIO_NUM_44   // D7  -> TFT DC
#define DISPLAY_SPI_RESET_PIN   GPIO_NUM_7      //GPIO_NUM_7    // D8  -> TFT RST

// This module/wiring may be more stable at 20 MHz if 40 MHz causes flicker.
#define DISPLAY_SPI_SCLK_HZ     (40 * 1000 * 1000)

// No touch panel in this wiring.
#define TP_PIN_NUM_TP_SDA   (GPIO_NUM_NC)
#define TP_PIN_NUM_TP_SCL   (GPIO_NUM_NC)
#define TP_PIN_NUM_TP_RST   (GPIO_NUM_NC)
#define TP_PIN_NUM_TP_INT   (GPIO_NUM_NC)

// No charge detect pins in this wiring. Avoid conflicts with D2/GPIO3.
#define POWER_CHARGE_LED_PIN GPIO_NUM_NC
#define POWER_CHARGE_DETECT_PIN GPIO_NUM_NC
#define POWER_ADC_UNIT ADC_UNIT_1
#define POWER_ADC_CHANNEL ADC_CHANNEL_0

#endif // _BOARD_CONFIG_H_