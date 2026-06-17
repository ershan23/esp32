# ESP32-S3 在线陌生人识别

这是一个基于 ESP32-S3 + OV2640 摄像头的在线陌生人检测项目。

本项目在 ESP32-S3 本地完成人脸检测和人脸识别，不依赖云端模型推理；同时通过 Wi-Fi 将实时画面和识别结果上传到服务器，让用户可以在浏览器中查看画面、识别状态和保存下来的陌生人图片。

相比离线 LCD/LED 版本，本在线版不再使用 LCD 屏幕和 LED 状态灯，而是把显示和告警信息放到网页端。

## 功能特性

- ESP32-S3 采集 OV2640 摄像头画面
- 使用 Espressif ESP-DL / ESP-WHO 人脸模型组件在本地检测和识别人脸
- 本地 SPIFFS 存储已注册用户的人脸特征库
- ESP32-S3 通过 Wi-Fi 上传 JPEG 画面和识别结果到服务器
- 浏览器实时查看摄像头画面和检测状态
- 支持按键注册人脸、清空人脸库
- 支持串口命令注册、清空、删除最后一个注册人脸
- 服务器自动保存 `FACE_UNKNOWN` / `FACE_USER_AND_UNKNOWN` 状态下的图片
- 网页端可查看服务器中保存的 unknown face 图片
- 服务器默认保留最近 7 天的 unknown face 图片，自动清理更早图片
- 浏览器访问需要用户名和密码登录

## 识别状态

网页和串口日志中会显示以下状态：

| 状态 | 含义 |
| --- | --- |
| `FACE_NONE` | 当前画面没有检测到人脸 |
| `FACE_USER` | 当前画面只有已注册用户 |
| `FACE_UNKNOWN` | 当前画面存在未注册人脸 |
| `FACE_USER_AND_UNKNOWN` | 当前画面同时存在已注册用户和未注册人脸 |

只要画面中存在任意未注册人脸，就会被判定为 unknown。

## 硬件要求

推荐硬件：

- ESP32-S3 开发板，建议带 PSRAM
- OV2640 摄像头
- 可选按键 2 个，用于注册人脸和清空人脸库

## 按键接线

代码已启用内部上拉电阻，按键按下时接地即可。

| 功能 | GPIO | 接线方式 |
| --- | --- | --- |
| 注册当前画面中的人脸 | GPIO1 | GPIO1 -> 按键 -> GND |
| 清空本地人脸库 | GPIO2 | GPIO2 -> 按键 -> GND |

注意：不要把按键接到 GPIO4 或 GPIO5，因为当前摄像头配置中：

- `GPIO4 = CAM_D0`
- `GPIO5 = CAM_D1`

## 摄像头引脚

当前 OV2640 摄像头引脚配置在 `components/CAMERA/camera.c` 中。

| 摄像头信号 | ESP32-S3 GPIO |
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

摄像头输出格式：

```text
RGB565 240x240
```

该格式用于本地人脸检测/识别，之后再在 ESP32-S3 上编码为 JPEG 上传到服务器。

## 项目目录

```text
.
├── CMakeLists.txt
├── dependencies.lock
├── partitions.csv
├── sdkconfig.defaults
├── components/
│   └── CAMERA/
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   ├── main.cpp
│   └── stream_config.h.example
└── server/
    ├── README.md
    ├── server.py
    └── server.js
```

说明：

- `main/main.cpp`：主程序，包含 Wi-Fi、上传、人脸检测、人脸识别、按键和串口命令逻辑
- `main/stream_config.h.example`：Wi-Fi 和服务器地址配置模板
- `components/CAMERA/`：摄像头初始化代码
- `server/server.py`：推荐使用的 Python 服务器，无第三方依赖
- `server/server.js`：Node.js 版本服务器，作为备用实现

`main/stream_config.h` 不会提交到仓库，因为它包含你的 Wi-Fi 名称和密码。

## ESP32-S3 配置

先复制配置模板：

```powershell
Copy-Item main\stream_config.h.example main\stream_config.h
```

然后编辑 `main/stream_config.h`：

```cpp
#define APP_WIFI_SSID "你的WiFi名称"
#define APP_WIFI_PASSWORD "你的WiFi密码"
#define APP_FRAME_UPLOAD_URL "http://你的服务器IP:8080/api/frame"
#define APP_UPLOAD_TOKEN ""
```

如果你希望限制只有指定设备可以上传，可以设置 `APP_UPLOAD_TOKEN`，同时服务器启动时设置相同的 `DEVICE_TOKEN`。

## 编译和烧录

在 ESP-IDF PowerShell 中执行：

```powershell
& "$env:IDF_PATH\export.ps1"
idf.py set-target esp32s3
idf.py -B build_stream build
idf.py -B build_stream -p COMx flash monitor
```

把 `COMx` 替换为你的实际串口号，例如：

```powershell
idf.py -B build_stream -p COM5 flash monitor
```

烧录后，串口日志中应看到类似信息：

```text
Upload URL: http://你的服务器IP:8080/api/frame
WiFi connected
```

如果看到：

```text
WiFi disabled: set APP_WIFI_SSID in main/stream_config.h
```

说明当前固件没有正确编译进 `main/stream_config.h`，请确认文件存在并重新完整构建、烧录。

## 服务器部署

推荐使用 Python 版本服务器，无需安装额外依赖。

在服务器上执行：

```bash
cd ESP32_Stranger\ Recognition_Online/server
VIEWER_USERNAME=admin VIEWER_PASSWORD=change_me UNKNOWN_RETENTION_DAYS=7 PORT=8080 python3 server.py
```

然后浏览器打开：

```text
http://你的服务器IP:8080/
```

默认功能：

- 浏览器访问需要登录
- ESP32-S3 通过 `POST /api/frame` 上传图片和识别状态
- 浏览器通过 `/api/latest.jpg` 获取最新画面
- 浏览器通过 `/api/status` 获取识别状态
- 浏览器通过 `/api/unknown` 获取已保存 unknown face 图片列表
- unknown face 图片通过 `/api/unknown-image/...` 查看
- unknown face 图片保存在服务器的 `unknown_faces/` 目录下
- 默认保留最近 7 天的 unknown face 图片，更早图片自动删除

## 网页登录

服务器启动时可通过环境变量设置用户名和密码：

```bash
VIEWER_USERNAME=admin
VIEWER_PASSWORD=change_me
```

示例：

```bash
VIEWER_USERNAME=admin VIEWER_PASSWORD=faceguard8080 PORT=8080 python3 server.py
```

## unknown face 图片保存

当 ESP32-S3 上传的状态中包含 `UNKNOWN` 时，服务器会保存当前 JPEG 图片。

保存目录：

```text
server/unknown_faces/
```

服务器部署到 `/root/esp32-face-server` 时，默认保存目录为：

```text
/root/esp32-face-server/unknown_faces/
```

图片会按日期分目录，例如：

```text
unknown_faces/20260617/unknown_20260617_191833_seq1001_faces1_users0.jpg
```

默认每 3 秒最多保存一张 unknown 图片，避免硬盘被快速写满。可通过环境变量调整：

```bash
UNKNOWN_SAVE_INTERVAL_MS=3000
```

默认保留最近 7 天图片：

```bash
UNKNOWN_RETENTION_DAYS=7
```

## 人脸注册和管理

注册当前画面中面积最大的人脸：

- 按下连接到 GPIO1 的按键
- 或在串口 monitor 输入 `e` 后回车

清空所有已注册人脸：

- 按下连接到 GPIO2 的按键
- 或在串口 monitor 输入 `c` 后回车

删除最后一个已注册人脸：

- 在串口 monitor 输入 `d` 后回车

## 人脸识别阈值

当前匹配阈值在 `main/main.cpp` 中：

```cpp
static constexpr float FACE_MATCH_THRESHOLD = 0.50f;
```

调参建议：

- 陌生人容易被误认为用户：提高到 `0.55` 或 `0.60`
- 已注册用户容易被判断为 unknown：降低到 `0.45`

## 注意事项

- 本项目的人脸检测和识别都在 ESP32-S3 本地完成，服务器只负责接收图片、展示结果和保存 unknown 图片。
- 摄像头画面、光照、角度、遮挡、口罩、眼镜都会影响识别稳定性。
- 建议每个用户在正常距离和光照下注册 2 到 3 次，提高稳定性。
- 不要提交 `main/stream_config.h`，该文件包含 Wi-Fi 密码。
- 如果服务器部署在云主机上，请确保安全组放行 TCP `8080` 端口，或自行通过 Nginx 反向代理到 80/443。
