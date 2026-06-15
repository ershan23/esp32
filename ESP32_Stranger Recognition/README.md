# ESP32-S3 离线人脸陌生人检测验证工程

本工程运行在 ESP32-S3 + OV2640 摄像头 + LCD 屏幕的边缘节点上，目标不是识别所有人的身份，而是判断画面中是否出现“已注册用户以外的人脸”。

工程基于 ESP-IDF，使用 Espressif 官方 ESP-DL / ESP-WHO 人脸模型组件，本地离线运行，不依赖云端。

## 当前功能

- 打开 OV2640 摄像头并在 LCD 上显示画面
- 检测画面中的人脸
- 注册本地用户人脸
- 将检测到的人脸逐张与本地人脸库比对
- 输出清晰串口日志：
  - `FACE_NONE`：无人脸
  - `FACE_USER`：只有已注册用户
  - `FACE_UNKNOWN`：存在未注册人脸
  - `FACE_USER_AND_UNKNOWN`：同时存在已注册用户和未注册人脸
- 通过四色 LED 指示状态
- 对识别状态做滤波，减少画面稳定时 LED 来回闪烁

## 硬件

建议硬件：

- MCU：ESP32-S3-WROOM-1，8MB PSRAM 版本
- 摄像头：OV2640 或兼容 ESP32 摄像头
- 屏幕：当前工程已有 LCD 驱动
- LED：红、绿、蓝、黄各 1 个
- 按键：KEY1、KEY2

当前摄像头和 LCD 已占用较多 GPIO。不要把按键接到 `GPIO4/GPIO5`，因为当前摄像头配置中：

- `GPIO4 = CAM_D0`
- `GPIO5 = CAM_D1`

## 按键接线

代码已启用内部上拉，按下为低电平。

| 功能 | GPIO | 接线 |
| --- | --- | --- |
| KEY1 注册人脸 | GPIO1 | 按键一端接 GPIO1，另一端接 GND |
| KEY2 清空人脸库 | GPIO2 | 按键一端接 GPIO2，另一端接 GND |

## LED 接线

当前代码按“低电平点亮”配置，也就是 GPIO 输出低电平时 LED 亮。

每个 LED 都要串联限流电阻，建议 `330Ω ~ 1kΩ`。

| 状态 | 颜色 | GPIO | 接线 |
| --- | --- | --- | --- |
| 发现未注册人脸 | 红灯 | GPIO8 | 3.3V -> 电阻 -> LED 正极，LED 负极 -> GPIO8 |
| 正常状态 | 绿灯 | GPIO41 | 3.3V -> 电阻 -> LED 正极，LED 负极 -> GPIO41 |
| 注册成功提示 | 蓝灯 | GPIO42 | 3.3V -> 电阻 -> LED 正极，LED 负极 -> GPIO42 |
| 清库成功提示 | 黄灯 | GPIO20 | 3.3V -> 电阻 -> LED 正极，LED 负极 -> GPIO20 |

注意：`GPIO20` 是 ESP32-S3 的 USB-JTAG 相关引脚之一。如果你的开发板依赖原生 USB-JTAG 下载或监视，请把黄灯换到其它空闲 GPIO，并同步修改 `main/main.cpp` 中的 `LED_YELLOW_GPIO`。

## LED 状态规则

LED 状态优先级如下：

1. 清空人脸库成功：黄灯亮 2 秒
2. 注册成功：蓝灯亮 2 秒
3. 检测到未注册人脸：红灯亮
4. 无异常、无人脸、空闲或只有已注册用户：绿灯亮

为减少抖动，代码对识别状态做了简单滤波：

- 连续 3 帧得到同一个新状态才切换
- 状态切换后至少保持 1.2 秒
- 蓝灯/黄灯动作反馈优先于识别状态

相关参数在 `main/main.cpp`：

```cpp
static constexpr int FACE_STATE_CONFIRM_FRAMES = 3;
static constexpr int64_t FACE_STATE_MIN_HOLD_US = 1200000;
static constexpr int64_t LED_EVENT_HOLD_US = 2000000;
```

## 模型方案

推荐使用 ESP-WHO 官方 `human_face_recognition` 示例的模型链路。本工程直接依赖 ESP-DL 官方模型组件：

- `espressif/human_face_detect`
- `espressif/human_face_recognition`
- `espressif/esp-dl`

当前默认模型：

- 人脸检测：`HumanFaceDetect::MSRMNP_S8_V1`
- 人脸特征提取：`HumanFaceFeat::MFN_S8_V1`
- 本地人脸库：SPIFFS 文件 `/spiffs/face.db`

处理链路：

1. 摄像头采集 `RGB565 240x240` 图像
2. 人脸检测模型检测全部人脸
3. 对每张人脸提取 embedding
4. 与本地已注册 embedding 库逐张比对
5. 只要存在任意未匹配人脸，就判定为 unknown

说明：官方 `HumanFaceRecognizer::recognize()` 在多人脸时只识别面积最大的人脸。为了满足“多张脸时只要存在未注册人脸就触发”，本工程没有直接使用该封装做最终判断，而是逐张脸提取 embedding 并查询本地库。

## 编译和烧录

在 ESP-IDF PowerShell 或已导入 ESP-IDF 环境的终端中执行：

```powershell
cd E:\esp-project\19_Camera
& "$env:IDF_PATH\export.ps1"
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

把 `COMx` 替换成实际串口号，例如 `COM5`。

当前工程已验证 `idf.py build` 通过。固件包含模型文件，因此使用了自定义分区表：

- app 分区：5MB
- SPIFFS 人脸库分区：约 3008KB

## 使用方法

烧录后打开串口监视器，设备会启动摄像头、LCD、人脸检测模型和本地人脸库。

注册人脸：

1. 让用户正对摄像头，保持脸部清晰完整。
2. 按下 `KEY1`，或在串口 monitor 输入 `e` 后回车。
3. 看到日志 `USER_ENROLLED id_count=1` 表示注册成功。
4. 蓝灯会亮 2 秒作为注册成功提示。

清空人脸库：

1. 按下 `KEY2`，或在串口 monitor 输入 `c` 后回车。
2. 看到日志 `FACE_DB_CLEARED` 表示清空成功。
3. 黄灯会亮 2 秒作为清库成功提示。

串口命令：

| 命令 | 功能 |
| --- | --- |
| `e` | 注册当前画面中面积最大的人脸 |
| `c` | 清空全部已注册人脸 |
| `d` | 删除最后一个已注册人脸 |

## unknown 判断

关键代码在 `main/main.cpp`：

```cpp
static constexpr float FACE_MATCH_THRESHOLD = 0.50f;
```

每张检测到的人脸都会执行：

1. `feat_model.run(img, face.keypoint)` 提取特征
2. `db.query_feat(feat, FACE_MATCH_THRESHOLD, FACE_QUERY_TOP_K)` 查询本地库
3. 查询为空则判定为 unknown

多人脸判断：

- 全部匹配：`FACE_USER`
- 全部不匹配：`FACE_UNKNOWN`
- 有匹配也有不匹配：`FACE_USER_AND_UNKNOWN`

## 调参建议

人脸匹配阈值：

```cpp
static constexpr float FACE_MATCH_THRESHOLD = 0.50f;
```

调参方向：

- 陌生人容易被认成用户：提高到 `0.55` 或 `0.60`
- 用户容易被认成 unknown：降低到 `0.45`

LED 状态仍然闪烁时：

```cpp
static constexpr int FACE_STATE_CONFIRM_FRAMES = 3;
static constexpr int64_t FACE_STATE_MIN_HOLD_US = 1200000;
```

调参方向：

- 状态切换太敏感：把 `FACE_STATE_CONFIRM_FRAMES` 调到 `5`
- LED 仍然跳变明显：把 `FACE_STATE_MIN_HOLD_US` 调到 `2000000`

## 关键文件

| 文件 | 说明 |
| --- | --- |
| `main/main.cpp` | 人脸检测、注册、unknown 判断、按键、LED 逻辑 |
| `main/idf_component.yml` | ESP-DL / 摄像头组件依赖 |
| `components/CAMERA/camera.c` | OV2640 摄像头引脚和帧格式配置 |
| `components/LCD/lcd.c` | LCD 显示驱动 |
| `partitions.csv` | 自定义分区表 |
| `FACE_RECOGNITION_VALIDATION.md` | 更详细的验证记录和模型说明 |

## 当前限制

- 光照过暗、强逆光、侧脸、大角度俯仰会降低检测和识别稳定性。
- 口罩、墨镜、遮挡会影响 embedding，可能把用户判成 unknown。
- 注册样本少时更容易误判，建议同一用户在正常距离和光照下注册 2 到 3 次。
- 当前默认 `MFN_S8_V1` 速度适合 ESP32-S3，但精度低于更大的 `MBF_S8_V1`。
- 当前默认检测模型 `MSRMNP_S8_V1` 较快，但远距离小脸弱于 `ESPDET_PICO_416_416_FACE`。
- 多人脸时会对每张脸提取特征，帧率会下降。

