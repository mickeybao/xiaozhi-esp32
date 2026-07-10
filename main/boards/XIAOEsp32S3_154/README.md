# ai聊天机器人

## 硬件说明
- ESP32
    - 型号
        - Seeed XIAO ESP32
    - 引脚
        - D0: GPIO1/A0/TOUCH1
        - D1: GPIO2/A1/TOUCH2
        - D2: GPIO3/A2/TOUCH3
        - D3: GPIO4/A3/TOUCH4
        - D4: GPIO5/A4/TOUCH5/SDA
        - D5: GPIO6/A5/TOUCH6/SCL
        - D6: GPIO43 / TX
        - D7: GPIO44 / RX
        - D8: GPIO7/A8/TOUCH7/SCK
        - D9: GPIO8/A9/TOUCH8
        - D10:GPIO9/A10/TOUCH9

- 显示屏
    - TFT:1.54英寸
    - 驱动:ST7789
    - 分辨率:240*240
    - 引脚：
        - GND:电源接地
        - VCC:电源正极
        - SCL:时钟
        - SDA:数据
        - RST:显示屏复位脚
        - DC:数据/命令控制引脚
        - CS:片选信号
        - BL:背光源
- 功放喇叭
    - 说明
        - MAX98357脉冲编码调制(PCM)即插即用扬声器放大器充分发挥Maxim引脚排列的灵活优势。相对于典型模拟放大器方案，大幅减少元件数量，从而大幅减小方案尺寸。由于大多数用户以数字上行方式控制音量，这些特性使用户能够充分利用Maxim的高性价比晶圆级封装(WLP)，无需昂贵PCBVIA。为了实现优异音，器件采用高抗噪能力的数字输入，并具有较高的抖动容限(优于竞争方案至少8dB)。器件也提供优异的电磁干扰(EMI)抑制，允许使用长线连接扬声器，无需增加外部滤波。此外，器件提供业界最高的D类效率。
        - 1.输出功率:4欧时为3.2W，THD为10%
                8欧时为1.8W,THD为10%
        - 2.I2S采样率:8Khz~96Khz 
        - 3.D类放大器增益可选:3dB/6dB/9dB/12dB/15dB
        - 4.无需主控时钟(MCLK)
        - 5.即插即用:只需单电源供电，即可自动配置35种不同的时钟和128种数字音频格式
        - 6.适用于树莓派Raspberry Pi, Arduino 及ESP32等工具,有I2S音频输出的微控制器或开发板系统
    - 引脚
        - VCC:电源正极，DC2.5V~5.5V
        - GND:接地
        - SD:关机和频道选择。SD_MODE拉低以将器件置于关断状态
        - GAIN:增益和频道选择。在TDM模式下，增益固定为12dB
        - DIN:数字输入信号
        - BCLK:位时钟输入
        - LRC:I2S与U模式的左/右时钟。同步时钟用于TDM模式
- 麦克风
    - 说明
        - INMP441是一款高性能，低功耗，数字输出，带底部端口的全向MEMS麦克风。该完整的INMP441解决方案由一个MEMS传感器，信号组成调节，模数转换器，抗混叠滤波器，电源管理和行业标准的24位12s接口。12s接口允许INMP441直接连接到数字处理器，如DSP和微控制器，无需使用用于系统中的音频编解码。INMP441具有高信噪比，是一款出色的选择近场应用。IN-MP441具有扁平宽带频率响应，导致自然声音高清晰度。
        - 1.具有高精度24位数据的数字12S接口
        - 2.高信噪比为61dBA
        - 3.高灵敏度-26dBFS
        - 4.从60Hz到15kHz的稳定频率响应
        - 5.低功耗:低电流消耗1.4 mA
        - 6.高PSR: -75 dBFS
    - 引脚
        - SCK:12s接口的串行数据时钟
        - WS:用于12s接口的串行数据字选择
        - L/R:左/右声道选择。
        - 设置为低电平时，麦克风在12s帧的左声道输出信号设置为高电平时，麦克风在右声道输出信号
        - SD:12S接口的串行数据输出。
        - VCC:输入电源，1.8V至3.3V.
        - GND:电源地

## 硬件连接
- 显示屏连接ESP32
    - 显示屏SCL-->ESP32 D10(GPIO9)
    - 显示屏SDA-->ESP32 D9(GPIO8)
    - 显示屏SD0悬空
    - 显示屏RST-->ESP32 D8(GPIO7)
    - 显示屏DC-->ESP32 D7(GPIO44)
    - 显示屏CS-->ESP32 D6(GPIO43)
    - 显示屏CSF悬空
    - 显示屏BL-->VCC
- 喇叭功放连接
    - MAX98357的LRC-->ESP32 D0(GPIO1)
    - MAX98357的BCLK-->ESP32 D1(GPIO2)
    - MAX98357的DIN-->ESP32 D2(GPIO3)
    - MAX98357的GAIN 悬空
    - MAX98357的SD 悬空
    - MAX98357的GND-->地
    - MAX98357的Vin-->VCC
- 麦克风连接
    - INMP441的L/R--> 地
    - INMP441的WS-->ESP32 D3(GPIO4)
    - INMP441的SCK-->ESP32 D4(GPIO5)
    - INMP441的SD-->GND D5(GPIO6)
    - INMP441的VDD-->VCC
    - INMP441的GND-->地

## AI参考
- https://github.com/78/xiaozhi-esp32



# 编译配置命令

**配置编译目标为 ESP32S3：**

```bash
idf.py set-target esp32s3
```

**打开 menuconfig：**

```bash
idf.py menuconfig
```

**选择板子：**

```
Xiaozhi Assistant -> Board Type -> XIAOEsp32S3_154
```

**编译：**

```bash
idf.py build
```
