# ESP32-S3 Plant Monitor with TinyML

这是一个基于 ESP32-S3 的植物环境监测与叶片健康识别项目。项目使用 LCD 显示光照、温湿度和 TinyML 推理结果；摄像头采集叶片画面，缩放/裁剪后送入 TFLite Micro 模型，在开发板端完成健康/病害二分类推理。

## 功能

- BH1750 光照传感器读取环境光照强度
- DHT22 读取空气湿度和温度
- OV2640 摄像头采集叶片画面
- ESP32-S3 本地运行 TFLite Micro int8 模型
- LCD 显示：
  - 光照 `LUX`
  - 湿度 `HUM`
  - 温度 `TMP`
  - AI 识别结果 `healthy` / `disease` / `NO LEAF` / `AIM LEAF`

## 硬件

- ESP32-S3-WROOM-1，建议带 PSRAM
- OV2640 摄像头
- SPI LCD 屏幕
- BH1750 光照传感器
- DHT22 温湿度传感器

## 接线

### BH1750

| BH1750 | ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO41 |
| SCL | GPIO42 |

BH1750 默认地址支持 `0x23`，代码也兼容 `0x5C`。

### DHT22

| DHT22 | ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| DATA | GPIO2 |

如果使用裸 DHT22，DATA 和 3V3 之间建议接一个 4.7K 到 10K 的上拉电阻。很多三针模块已经自带上拉电阻。

### LCD

| LCD 信号 | ESP32-S3 |
| --- | --- |
| MOSI | GPIO11 |
| MISO | GPIO13 |
| SCLK | GPIO12 |
| CS | GPIO48 |
| DC | GPIO47 |
| RST | GPIO21 |
| BLK | GPIO40 |

### OV2640 摄像头

| 摄像头信号 | ESP32-S3 |
| --- | --- |
| PWDN | GPIO10 |
| RESET | GPIO9 |
| SIOD | GPIO39 |
| SIOC | GPIO38 |
| D0 | GPIO4 |
| D1 | GPIO5 |
| D2 | GPIO6 |
| D3 | GPIO7 |
| D4 | GPIO15 |
| D5 | GPIO16 |
| D6 | GPIO17 |
| D7 | GPIO18 |
| VSYNC | GPIO14 |
| HREF | GPIO3 |
| PCLK | GPIO45 |

## 构建和烧录

先进入项目目录：

```powershell
cd E:\esp-project\19_Camera
```

加载 ESP-IDF 环境：

```powershell
& 'E:\.espressif\v6.0.1\esp-idf\export.ps1'
```

构建：

```powershell
idf.py build
```

烧录并打开串口监视器：

```powershell
idf.py -p COM3 flash monitor
```

如果 `COM3` 不存在，请在设备管理器里查看实际串口号，或用下面命令查看：

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name,Description
```

## TinyML 模型

当前模型文件位于：

```text
models/plant_binary/plant_binary_int8.tflite
```

模型已经转换为 C 数组并编译进固件：

```text
components/ML/generated/plant_binary_model.c
components/ML/generated/plant_binary_model.h
```

模型输入：

```text
96 x 96 x 3, int8
```

模型输出：

```text
healthy / disease
```

代码会先从摄像头画面中估计叶片区域，再裁剪叶片 ROI 缩放到 96x96 后送入模型。

## AI 显示逻辑

LCD 第 6 行显示 AI 结果：

```text
AI:healthy xx%
AI:disease xx%
AI:NO LEAF
AI:AIM LEAF
```

含义：

- `healthy`：模型判断为健康叶片
- `disease`：模型高把握判断为病害叶片
- `NO LEAF`：画面中几乎没有检测到叶片特征
- `AIM LEAF`：叶片太小或画面不可靠，需要把摄像头对准叶片

为了减少误报，代码目前采用保守病害阈值：

```cpp
constexpr float kDiseaseMinConfidence = 0.95f;
constexpr float kDiseaseMinMargin = 0.20f;
```

也就是只有 `disease_score >= 95%`，并且比 `healthy_score` 至少高 20%，才显示 `disease`。

## 注意事项

当前模型是 `healthy / disease` 二分类模型。二分类模型没有真正的“不确定”类别，所以在背景复杂、叶片很小、光照异常或摄像头画面和训练数据差异较大时，softmax 可能显示很高的置信度。

如果要做得更稳定，建议下一步训练三分类模型：

```text
no_leaf / healthy / disease
```

并加入 ESP32-S3 摄像头实际拍摄的数据，包括：

- 正常健康叶片
- 明显病害叶片
- 非叶片背景
- 手持、桌面、不同光照、不同距离和角度的画面

## 目录结构

```text
.
|-- components/
|   |-- CAMERA/        # OV2640 摄像头初始化
|   |-- LCD/           # LCD 显示驱动
|   |-- ML/            # TFLite Micro 推理代码
|   |-- SENSOR/        # BH1750 和 DHT22
|   |-- SPI/           # SPI 初始化
|   `-- LED/           # 板载 LED
|-- main/              # 应用主循环
|-- models/            # 训练后的 TFLite 模型和标签
|-- tools/ml/          # 数据准备、训练、导出工具
|-- CMakeLists.txt
|-- sdkconfig
`-- README.md
```

## 常见问题

### 为什么 disease 经常显示 100%？

当前模型输出是 int8 softmax，最大输出解量化后约为 99.6%，LCD 四舍五入会显示为 100%。这并不代表绝对可靠，只表示二分类 softmax 在当前输入上饱和。

### 为什么对准叶子仍然显示 NO LEAF 或 AIM LEAF？

可能是叶片在画面中占比太小、光照过暗/过曝、摄像头没有对焦，或叶片颜色不符合当前轻量叶片检测规则。可以先调整距离和光照，也可以在 `components/ML/ml_classifier.cpp` 中调整叶片检测阈值。

### 为什么需要 PSRAM？

摄像头帧缓存和 TFLite Micro tensor arena 都会占用较多内存。建议使用带 PSRAM 的 ESP32-S3 开发板。
