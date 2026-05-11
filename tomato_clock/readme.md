# Tomato Clock (番茄钟)

基于 ESP32-S3-WROOM-1 的番茄钟，使用 LCD 屏幕显示计时，不同颜色 LED 指示工作/休息阶段。

## 硬件连接

| 外设    | 引脚    | 说明         |
|--------|---------|-------------|
| LED1   | GPIO38  | 蓝灯 (就绪)   |
| LED2   | GPIO39  | 红灯 (工作)   |
| LED3   | GPIO41  | 黄灯 (短休息)  |
| LED4   | GPIO42  | 绿灯 (长休息)  |
| KEY1   | GPIO9   | 按键确认      |
| LCD    | SPI2    | ST7789 240x240 |

LCD SPI 引脚: MOSI=GPIO11, SCLK=GPIO12, CS=GPIO48, DC=GPIO47, RST=GPIO21, BLK=GPIO40

## 工作流程

```
READY ──[KEY1]──> WORK (25min) ──[计时结束]──> WORK_DONE ──[KEY1]──> SHORT_BREAK (5min)
  ^                                                                        |
  |                                                                   [计时结束]
  |                                                                        v
  |                                                              SHORT_BREAK_DONE
  |                                                                        |
  |                                                                   [KEY1]
  |                                                                        |
  |                                                    (未满4次则回到 WORK)
  |                                                                        
LONG_BREAK_DONE <──[计时结束]── LONG_BREAK (15min) <──[KEY1]── WORK_DONE (第4次)
      |
   [KEY1]
      |
   回到 READY
```

## 状态说明

| 状态               | LED  | 显示内容                      | 时长    |
|-------------------|------|------------------------------|--------|
| READY             | 蓝灯 | POMODORO / PRESS KEY1 TO START | —    |
| WORK              | 红灯 | WORK TIME + 会话计数(N/4) + 倒计时 | 25分钟 |
| WORK_DONE         | 红灯 | WORK DONE! / PRESS KEY1       | —      |
| SHORT_BREAK       | 黄灯 | SHORT BREAK + 倒计时           | 5分钟  |
| SHORT_BREAK_DONE  | 黄灯 | BREAK OVER! / PRESS KEY1      | —      |
| LONG_BREAK        | 绿灯 | LONG BREAK + 倒计时            | 15分钟 |
| LONG_BREAK_DONE   | 绿灯 | BREAK OVER! / PRESS KEY1      | —      |

- 每个工作阶段结束后需按 KEY1 确认，才会进入休息
- 每完成 4 个工作阶段后进入长休息（15分钟），否则进入短休息（5分钟）
- 工作倒计时最后 5 分钟数字变紫色，最后 1 分钟变黄色

## 构建与烧录

使用 ESP-IDF (v5.0+) 构建:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

## 项目结构

```
tomato_clock/
├── main/
│   ├── main.c              # 番茄钟状态机主程序
│   └── CMakeLists.txt
├── components/
│   ├── LED/                # LED 驱动 (GPIO控制)
│   ├── LCD/                # ST7789 LCD 驱动 (SPI)
│   ├── SPI/                # SPI2 总线驱动
│   ├── GPTIM/              # 通用定时器 (500ms周期)
│   └── CMakeLists.txt
├── CMakeLists.txt
└── README.md
```
