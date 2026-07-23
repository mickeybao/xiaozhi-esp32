# Waveshare ESP32-S3-Touch-LCD-2

[ESP32-S3-Touch-LCD-2](https://docs.waveshare.net/ESP32-S3-Touch-LCD-2) 
### ESP32-S3-Touch-LCD-2
- 参考链接 https://docs.waveshare.net/ESP32-S3-Touch-LCD-2
- 相关资料 https://docs.waveshare.net/ESP32-S3-Touch-LCD-2/Resources-And-Documents
- IDF开发 https://docs.waveshare.net/ESP32-S3-Touch-LCD-2/Development-Environment-Setup-ESP-IDF
- 产品特性
    - 搭载 ESP32-S3R8 高性能 Xtensa 32 位 LX7 双核处理器，主频高达 240 MHz
    - 支持 2.4 GHz Wi-Fi (802.11 b/g/n) 和 Bluetooth 5 (LE)，板载天线
    - 内置 512KB 的 SRAM 和 384KB ROM，叠封 8MB PSRAM 和外接 16MB Flash
    - 采用 Type-C 接口，紧跟时代潮流，无需纠结正反插
    - 板载 2 英寸电容触摸 LCD 屏，240 × 320 分辨率，262K 彩色，能清晰地显示彩色图片
    - 内置 ST7789T3 驱动芯片和 CST816D 电容触控芯片，可分别使用 SPI 和 I2C 接口通信，不占用过多接口引脚资源
    - 板载 QMI8658 六轴惯性测量单元 (3 轴加速度、3 轴陀螺仪）
    - 板载 3.7V MX1.25 锂电池充放电接口
    - 板载 USB Type-C 接口，可用于供电和下载调试，方便开发使用
    - 板载 Micro SD 卡槽，可外接 SD 卡存储图片或文件
    - 引出 22 个 GPIO，可灵活配置外设功能
    - 板载摄像头接口，兼容 OV2640 和 OV5640 等主流摄像头，适用于图像和视频采集
- 引脚说明
    - 1 3v3
    - 2 GND
    - 3 TXD
    - 4 RXD
    - 5 GPIO47
    - 6 GPIO48
    - 7 GPIO15
    - 8 GPIO13
    - 9 GPIO11
    - 10 GPIO12
    - 11 GPIO14
    - 12 GPIO9
    - 13 GND
    - 14 VBAT
    - 15 5V
    - 16 GND
    - 17 GPIO19
    - 18 GPIO 20
    - 19 GPIO10
    - 20 GPIO7
    - 21 GPIO8
    - 22 GPIO21
    - 23 GPIO18
    - 24 GPIO17
    - 25 GPIO16
    - 26 GPIO6
    - 27 GPIO4
    - 28 GPIO2
### 麦克风
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
### 功放喇叭
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

### 4G模块 ML307R
新一代LTECAT1系列TINY核心板是基于ML307R模组系列设计的纯数传业内最小核心板，产品贴合时下AI概念，设计了跟市面上绝大多数Al小智的主控兼容的方案，是目前尺寸最小的超薄款数传核心板，19mm*20mm，能够满足超小尺寸的应用
- 引脚定义
    - 1 VIN 5-16V
    - 2 GND 地
    - 3 TXD 串口发送，3.3V电平
    - 4 RXD  串口发送，3.3V电平
    - 5 EN 默认上拉到VIN
    - 6 BAT 电池供电引脚 3.4-4.2V 
        - 不可与VIN同时供电
        - 通过此引脚供电 EN无效

### 引脚连接
- ESP32-S3-Touch-LCD-2 
    - 28脚 连接 麦克风 WS
    - 27脚 连接 麦克风 SCK
    - 26脚 连接 麦克风 SD
    - 21脚 连接 功放LRC
    - 20脚 连接 功放BCLK
    - 19脚 连接 功放DIN
    - 18脚 连接 功放GAIN
    - 17脚 连接 功放SD
    - 3脚  连接 4G模块RXD
    - 4脚  连接 4G模块TXD


