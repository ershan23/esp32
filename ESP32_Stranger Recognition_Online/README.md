# ESP32 Stranger Recognition Online

ESP32-S3 + OV2640 online stranger detection project.

This version keeps face detection and face recognition on the ESP32-S3, but moves the display result to a browser. The device uploads the latest camera frame and recognition status to a server, and the web page shows the live image, face state, stored unknown-face snapshots, and runtime metadata.

## Features

- OV2640 camera capture on ESP32-S3
- Local face detection and recognition with Espressif ESP-DL / ESP-WHO model components
- Local face enrollment database stored in SPIFFS
- Browser live preview through Wi-Fi upload
- Detection states:
  - `FACE_NONE`
  - `FACE_USER`
  - `FACE_UNKNOWN`
  - `FACE_USER_AND_UNKNOWN`
- Unknown-face JPEG snapshots saved on the server
- Unknown snapshot retention: newest 7 days by default
- Password-protected browser page
- LCD and LED display logic removed from the main workflow

## Hardware

Recommended hardware:

- ESP32-S3 module with PSRAM
- OV2640 camera
- Optional buttons for enrollment and clearing the local face database

### Button Wiring

The firmware enables internal pull-up resistors. Buttons should pull the GPIO to GND when pressed.

| Function | GPIO | Wiring |
| --- | --- | --- |
| Enroll current face | GPIO1 | GPIO1 -> button -> GND |
| Clear face database | GPIO2 | GPIO2 -> button -> GND |

Do not use GPIO4 or GPIO5 for buttons in this hardware layout because they are used by the camera data pins.

## Project Layout

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

`main/stream_config.h` is intentionally not committed because it contains local Wi-Fi credentials.

## ESP32 Configuration

Copy the example config:

```powershell
Copy-Item main\stream_config.h.example main\stream_config.h
```

Edit `main/stream_config.h`:

```cpp
#define APP_WIFI_SSID "your_wifi_ssid"
#define APP_WIFI_PASSWORD "your_wifi_password"
#define APP_FRAME_UPLOAD_URL "http://your_server_ip:8080/api/frame"
#define APP_UPLOAD_TOKEN ""
```

If `APP_UPLOAD_TOKEN` is set, start the server with the same `DEVICE_TOKEN` value.

## Build and Flash

Use an ESP-IDF PowerShell or import ESP-IDF first:

```powershell
& "$env:IDF_PATH\export.ps1"
idf.py set-target esp32s3
idf.py -B build_stream build
idf.py -B build_stream -p COMx flash monitor
```

Replace `COMx` with the actual serial port, for example `COM5`.

Expected startup logs:

```text
Upload URL: http://your_server_ip:8080/api/frame
WiFi connected
```

## Server

The Python server has no third-party dependency.

On the server:

```bash
cd ESP32_Stranger\ Recognition_Online/server
VIEWER_USERNAME=admin VIEWER_PASSWORD=change_me UNKNOWN_RETENTION_DAYS=7 PORT=8080 python3 server.py
```

Open:

```text
http://your_server_ip:8080/
```

The default server behavior:

- Browser access requires login
- ESP32 uploads frames to `/api/frame`
- Latest frame is served by `/api/latest.jpg`
- Status is served by `/api/status`
- Unknown-face list is served by `/api/unknown`
- Unknown-face images are stored under `unknown_faces/`
- Unknown-face images older than 7 days are deleted automatically

## Face Enrollment

You can enroll a user face in two ways:

- Press the button connected to GPIO1
- Type `e` in the serial monitor and press Enter

Clear the face database:

- Press the button connected to GPIO2
- Type `c` in the serial monitor and press Enter

Delete the last enrolled face:

- Type `d` in the serial monitor and press Enter

## Notes

- Recognition is performed locally on the ESP32-S3. The server stores only uploaded JPEG frames and state metadata.
- Unknown-face snapshots are captured from frames whose stable or raw state contains `UNKNOWN`.
- The server login password protects the browser page and image browsing APIs. The ESP32 upload endpoint is intentionally separate and can be protected with `APP_UPLOAD_TOKEN` / `DEVICE_TOKEN`.
- Do not commit `main/stream_config.h`; it contains Wi-Fi credentials.
