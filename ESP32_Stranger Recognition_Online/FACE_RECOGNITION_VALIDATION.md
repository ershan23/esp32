# ESP32-S3 face unknown validation

## 推荐方案

推荐使用 Espressif 官方 ESP-WHO 的 `examples/human_face_recognition` 思路，但在本工程中直接依赖 ESP-DL 模型组件：

- `espressif/human_face_detect` 0.4.2
- `espressif/human_face_recognition` 0.3.2
- `espressif/esp-dl` 3.3.5

默认模型配置：

- 人脸检测：`HumanFaceDetect::MSRMNP_S8_V1`
- 人脸特征：`HumanFaceFeat::MFN_S8_V1`
- 模型位置：flash rodata，随 app 固件打包
- 人脸库位置：SPIFFS 文件 `/spiffs/face.db`

官方 `HumanFaceRecognizer::recognize()` 在多人脸时只识别面积最大的人脸。本工程为了满足“只要存在未注册人脸就触发”，改为：

1. 检测全部人脸。
2. 对每一张检测到的人脸分别提取 embedding。
3. 对每个 embedding 查询本地 `dl::recognition::DataBase`。
4. 只要任意一张脸没有匹配到已注册用户，就标记 unknown。

## 模型链路

1. OV2640 输出 `RGB565`、`240x240` 帧，帧缓冲在 PSRAM。
2. `HumanFaceDetect::MSRMNP_S8_V1` 运行人脸检测。
3. `HumanFaceFeat::MFN_S8_V1` 按检测结果中的关键点提取 embedding。
4. `dl::recognition::DataBase` 从 `/spiffs/face.db` 读取已注册 embedding。
5. `query_feat(feat, FACE_MATCH_THRESHOLD, 1)` 判断是否匹配。
6. 日志输出：
   - 无人脸：`FACE_NONE`
   - 仅注册用户：`FACE_USER`
   - 仅未注册人脸：`FACE_UNKNOWN`
   - 同时存在注册用户和未注册人脸：`FACE_USER_AND_UNKNOWN`

## 硬件条件

- MCU：ESP32-S3-WROOM-1
- PSRAM：建议 8MB，当前 `sdkconfig` 已启用 `CONFIG_SPIRAM=y`
- 摄像头：OV2640，当前工程使用 `components/CAMERA/camera.c` 中的 DVP 引脚
- LCD：沿用当前 `components/LCD`
- Flash：当前分区表按 8MB flash 规划，app 5MB，SPIFFS 约 3008KB

注意：当前摄像头引脚 `D0=GPIO4`、`D1=GPIO5`，与最初的 `KEY1=GPIO4`、`KEY2=GPIO5` 冲突。现在代码已改为 `KEY1=GPIO1`、`KEY2=GPIO2`。

## 按键和 LED 接线

按键接线：

- `KEY1`：一端接 `GPIO1`，另一端接 `GND`
- `KEY2`：一端接 `GPIO2`，另一端接 `GND`

代码启用内部上拉，按下为低电平。

LED 接线默认使用低电平点亮。每个 LED 都需要串联限流电阻，建议 330 到 1k 欧姆：

- 红灯：`3.3V -> 电阻 -> 红 LED 正极`，LED 负极接 `GPIO8`
- 绿灯：`3.3V -> 电阻 -> 绿 LED 正极`，LED 负极接 `GPIO41`
- 蓝灯：`3.3V -> 电阻 -> 蓝 LED 正极`，LED 负极接 `GPIO42`
- 黄灯：`3.3V -> 电阻 -> 黄 LED 正极`，LED 负极接 `GPIO20`

GPIO20 是 ESP32-S3 的 USB-JTAG 相关引脚之一。当前工程主串口日志使用 UART0，因此可用于验证；如果你的板子依赖原生 USB-JTAG 下载或监视，请把黄灯改到其它空闲 GPIO，并同步修改 `LED_YELLOW_GPIO`。

LED 状态优先级：

- 检测到未注册人脸：红灯亮
- 清空人脸库成功：黄灯亮 2 秒
- 注册成功：蓝灯亮 2 秒
- 无异常、无人脸、空闲或仅有已注册用户：绿灯亮

为避免识别临界值抖动造成 LED 乱闪，代码对人脸状态做了滤波：

- 连续 3 帧得到同一新状态才切换
- 状态切换后至少保持 1.2 秒
- 注册/清库的蓝灯/黄灯动作反馈优先于识别状态

## 编译和烧录

在 ESP-IDF PowerShell 或导入环境后执行：

```powershell
cd E:\esp-project\19_Camera
& "$env:IDF_PATH\export.ps1"
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

本次已验证 `idf.py build` 通过：

- app 大小：`0x38a990`
- app 分区：`0x500000`
- 剩余空间：约 29%

## 注册用户人脸

1. 烧录后打开串口监视器。
2. 正对摄像头，确保画面中有清晰、完整的人脸。
3. 在 monitor 输入 `e` 并回车。
4. 日志出现 `USER_ENROLLED id_count=1` 表示至少注册 1 个用户成功。

其他串口命令：

- `e`：注册当前画面中面积最大的人脸。
- `d`：删除最后一个注册用户。
- `c`：清空全部注册用户。

## unknown 判断

关键逻辑在 `main/main.cpp`：

- `FACE_MATCH_THRESHOLD = 0.50f`
- `classify_faces()` 对每张脸调用 `feat_model.run(img, face.keypoint)`
- 然后调用 `db.query_feat(feat, FACE_MATCH_THRESHOLD, FACE_QUERY_TOP_K)`
- 查询结果为空就是 unknown

多张脸时：

- 全部匹配：`FACE_USER`
- 全部不匹配：`FACE_UNKNOWN`
- 至少一张匹配、至少一张不匹配：`FACE_USER_AND_UNKNOWN`

## 阈值调参

位置：`main/main.cpp`

```cpp
static constexpr float FACE_MATCH_THRESHOLD = 0.50f;
```

调参方向：

- 误把陌生人认成用户：提高阈值，例如 `0.55`、`0.60`
- 经常把用户认成 unknown：降低阈值，例如 `0.45`

检测阈值在官方组件：

- `managed_components/espressif__human_face_detect/human_face_detect.hpp`
- `MSR::default_score_thr = 0.5`
- `MNP::default_score_thr = 0.5`

如要调检测阈值，需要在创建 `HumanFaceDetect` 后通过底层 `set_score_thr()` 调整，或改用更强但更慢的 `ESPDET_PICO_224_224_FACE` / `ESPDET_PICO_416_416_FACE`。

## 关键源码

- 验证主逻辑：`main/main.cpp`
- 依赖声明：`main/idf_component.yml`
- 分区表：`partitions.csv`
- 摄像头引脚和 RGB565 配置：`components/CAMERA/camera.c`
- 官方检测组件：`managed_components/espressif__human_face_detect`
- 官方识别组件：`managed_components/espressif__human_face_recognition`
- ESP-WHO 官方参考 example：`../_esp_who_ref/examples/human_face_recognition`

## 限制

- 光照过暗、强逆光、侧脸、大角度俯仰会明显降低检测和识别稳定性。
- 口罩、墨镜、遮挡会影响 embedding，可能把用户判成 unknown。
- 注册样本少时更容易误判，建议同一用户在正常使用距离和光照下注册 2 到 3 次。
- 当前默认 `MFN_S8_V1` 速度较适合 ESP32-S3，但精度低于更大的 `MBF_S8_V1`。
- 当前默认检测模型 `MSRMNP_S8_V1` 快，但小脸/远距离脸弱于 `ESPDET_PICO_416_416_FACE`。
- 本验证工程串行处理每帧检测和每张脸识别，多人脸时帧率会下降。
- GPIO4/GPIO5 与现有摄像头 D0/D1 冲突，按键未默认启用。
