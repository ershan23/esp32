# ESP32-S3 viewer server

Run this on `62.234.54.71`.

Python, no extra dependencies:

```bash
cd server
VIEWER_USERNAME=admin VIEWER_PASSWORD=faceguard8080 UNKNOWN_RETENTION_DAYS=7 PORT=8080 python3 server.py
```

Node.js version:

```bash
cd server
PORT=8080 node server.js
```

Then open:

```text
http://62.234.54.71:8080/
```

If you set `APP_UPLOAD_TOKEN` in `main/stream_config.h`, start the server with the same value:

```bash
DEVICE_TOKEN=your_token VIEWER_PASSWORD=your_viewer_password PORT=8080 python3 server.py
```

Unknown frames are saved under:

```text
/root/esp32-face-server/unknown_faces
```

The server keeps the newest 7 days of unknown frames by default. Older `.jpg` files are removed on startup and then during periodic cleanup while frames are being received.
