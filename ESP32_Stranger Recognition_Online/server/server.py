from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import base64
import hashlib
import json
import os
from pathlib import Path
import time
from urllib.parse import parse_qs, quote, unquote, urlparse


HOST = os.environ.get("HOST", "0.0.0.0")
PORT = int(os.environ.get("PORT", "8080"))
DEVICE_TOKEN = os.environ.get("DEVICE_TOKEN", "")
VIEWER_USERNAME = os.environ.get("VIEWER_USERNAME", "admin")
VIEWER_PASSWORD = os.environ.get("VIEWER_PASSWORD", "faceguard8080")
UNKNOWN_DIR = Path(os.environ.get("UNKNOWN_DIR", "/root/esp32-face-server/unknown_faces"))
UNKNOWN_SAVE_INTERVAL_MS = int(os.environ.get("UNKNOWN_SAVE_INTERVAL_MS", "3000"))
UNKNOWN_RETENTION_DAYS = int(os.environ.get("UNKNOWN_RETENTION_DAYS", "7"))
UNKNOWN_CLEANUP_INTERVAL_MS = int(os.environ.get("UNKNOWN_CLEANUP_INTERVAL_MS", "3600000"))
SESSION_COOKIE_NAME = "face_guard_session"
SESSION_TOKEN = hashlib.sha256(f"{VIEWER_USERNAME}:{VIEWER_PASSWORD}".encode("utf-8")).hexdigest()

latest_frame = None
latest_meta = {
    "state": "WAITING_FOR_FRAME",
    "raw": "WAITING_FOR_FRAME",
    "faces": 0,
    "users": 0,
    "seq": 0,
    "updatedAt": None,
    "unknownSaved": 0,
}
last_unknown_saved_ms = 0
last_unknown_cleanup_ms = 0
unknown_saved_count = 0

PAGE = """<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Face Guard</title>
  <style>
    :root { color-scheme: dark; font-family: Arial, sans-serif; background: #101418; color: #f5f7fa; }
    body { margin: 0; min-height: 100vh; display: grid; grid-template-rows: auto 1fr; }
    header { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 14px 18px; background: #171d24; border-bottom: 1px solid #2a323c; }
    h1 { margin: 0; font-size: 18px; font-weight: 700; }
    main { display: grid; grid-template-columns: minmax(0, 1fr) 280px; gap: 18px; padding: 18px; box-sizing: border-box; }
    .viewer { min-width: 0; display: grid; place-items: center; background: #080b0e; border: 1px solid #2a323c; border-radius: 8px; overflow: hidden; }
    #frame { width: min(100%, 820px); image-rendering: auto; aspect-ratio: 1 / 1; object-fit: contain; display: block; }
    aside { display: grid; align-content: start; gap: 10px; }
    .metric { border: 1px solid #2a323c; border-radius: 8px; padding: 12px; background: #171d24; }
    .label { color: #9fb0c3; font-size: 12px; margin-bottom: 6px; }
    .value { font-size: 18px; font-weight: 700; overflow-wrap: anywhere; }
    .danger { color: #ff6868; }
    .ok { color: #6ee7a8; }
    .unknown-panel { padding: 0 18px 18px; }
    .unknown-head { display: flex; align-items: center; justify-content: space-between; margin-bottom: 10px; }
    .unknown-head h2 { margin: 0; font-size: 16px; }
    .unknown-head span { color: #9fb0c3; font-size: 12px; }
    .unknown-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(150px, 1fr)); gap: 12px; }
    .unknown-card { border: 1px solid #2a323c; border-radius: 8px; overflow: hidden; background: #171d24; color: inherit; text-decoration: none; }
    .unknown-card img { width: 100%; aspect-ratio: 1 / 1; object-fit: cover; display: block; background: #080b0e; }
    .unknown-card div { padding: 8px; color: #c9d4df; font-size: 12px; overflow-wrap: anywhere; }
    @media (max-width: 760px) { main { grid-template-columns: 1fr; } aside { grid-template-columns: repeat(2, minmax(0, 1fr)); } }
  </style>
</head>
<body>
  <header><h1>ESP32-S3 Face Guard</h1><span id="status">connecting</span></header>
  <main>
    <section class="viewer"><img id="frame" alt="latest camera frame"></section>
    <aside>
      <div class="metric"><div class="label">stable result</div><div class="value" id="state">-</div></div>
      <div class="metric"><div class="label">raw result</div><div class="value" id="raw">-</div></div>
      <div class="metric"><div class="label">faces</div><div class="value" id="faces">0</div></div>
      <div class="metric"><div class="label">registered users</div><div class="value" id="users">0</div></div>
      <div class="metric"><div class="label">frame seq</div><div class="value" id="seq">0</div></div>
      <div class="metric"><div class="label">age</div><div class="value" id="age">-</div></div>
      <div class="metric"><div class="label">unknown saved</div><div class="value" id="unknownSaved">0</div></div>
    </aside>
  </main>
  <section class="unknown-panel">
    <div class="unknown-head">
      <h2>Unknown Faces</h2>
      <span id="unknownCount">0 stored</span>
    </div>
    <div class="unknown-grid" id="unknownGrid"></div>
  </section>
  <script>
    const frame = document.getElementById("frame");
    const unknownGrid = document.getElementById("unknownGrid");
    const ids = ["state", "raw", "faces", "users", "seq", "age", "unknownSaved"];
    const setText = (id, value) => document.getElementById(id).textContent = value;
    const formatTime = (ms) => new Date(ms).toLocaleString();

    async function refreshMeta() {
      try {
        const res = await fetch("/api/status", { cache: "no-store" });
        const meta = await res.json();
        ids.forEach((id) => setText(id, id === "age" ? (meta.ageMs == null ? "-" : Math.round(meta.ageMs) + " ms") : (meta[id] ?? "-")));
        document.getElementById("state").className = "value " + (String(meta.state).includes("UNKNOWN") ? "danger" : "ok");
        document.getElementById("status").textContent = meta.updatedAt ? "online" : "waiting";
      } catch {
        document.getElementById("status").textContent = "server unavailable";
      }
    }

    function refreshFrame() {
      frame.src = "/api/latest.jpg?t=" + Date.now();
    }

    async function refreshUnknown() {
      try {
        const res = await fetch("/api/unknown", { cache: "no-store" });
        const data = await res.json();
        document.getElementById("unknownCount").textContent = data.files.length + " stored";
        unknownGrid.innerHTML = data.files.map((file) => `
          <a class="unknown-card" href="${file.url}" target="_blank" rel="noopener">
            <img src="${file.url}" loading="lazy" alt="${file.name}">
            <div>${formatTime(file.mtime)}<br>${file.name}</div>
          </a>
        `).join("");
      } catch {
        document.getElementById("unknownCount").textContent = "unavailable";
      }
    }

    setInterval(refreshMeta, 500);
    setInterval(refreshFrame, 250);
    setInterval(refreshUnknown, 10000);
    refreshMeta();
    refreshFrame();
    refreshUnknown();
  </script>
</body>
</html>"""

LOGIN_PAGE = """<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Face Guard Login</title>
  <style>
    :root { color-scheme: dark; font-family: Arial, sans-serif; background: #101418; color: #f5f7fa; }
    body { margin: 0; min-height: 100vh; display: grid; place-items: center; }
    form { width: min(360px, calc(100vw - 32px)); display: grid; gap: 12px; }
    h1 { margin: 0 0 8px; font-size: 22px; }
    label { display: grid; gap: 6px; color: #9fb0c3; font-size: 13px; }
    input { box-sizing: border-box; width: 100%; border: 1px solid #2a323c; border-radius: 8px; padding: 12px; background: #171d24; color: #f5f7fa; font-size: 16px; }
    button { border: 0; border-radius: 8px; padding: 12px; background: #6ee7a8; color: #07120c; font-size: 16px; font-weight: 700; cursor: pointer; }
    .error { min-height: 20px; color: #ff6868; font-size: 13px; }
  </style>
</head>
<body>
  <form method="post" action="/login">
    <h1>ESP32-S3 Face Guard</h1>
    <label>Username<input name="username" autocomplete="username" autofocus></label>
    <label>Password<input name="password" type="password" autocomplete="current-password"></label>
    <button type="submit">Login</button>
    <div class="error">__ERROR__</div>
  </form>
</body>
</html>"""


def is_unknown_state(meta):
    return "UNKNOWN" in str(meta.get("state", "")) or "UNKNOWN" in str(meta.get("raw", ""))


def cleanup_unknown_faces(force=False):
    global last_unknown_cleanup_ms
    now_ms = int(time.time() * 1000)
    if not force and now_ms - last_unknown_cleanup_ms < UNKNOWN_CLEANUP_INTERVAL_MS:
        return 0
    last_unknown_cleanup_ms = now_ms

    cutoff = time.time() - UNKNOWN_RETENTION_DAYS * 24 * 60 * 60
    removed = 0
    if not UNKNOWN_DIR.exists():
        return removed

    for path in UNKNOWN_DIR.rglob("*.jpg"):
        try:
            if path.stat().st_mtime < cutoff:
                path.unlink()
                removed += 1
        except OSError:
            pass

    for directory in sorted([p for p in UNKNOWN_DIR.rglob("*") if p.is_dir()], reverse=True):
        try:
            directory.rmdir()
        except OSError:
            pass
    return removed


def save_unknown_frame(frame, meta):
    global last_unknown_saved_ms, unknown_saved_count
    cleanup_unknown_faces()
    now_ms = int(time.time() * 1000)
    if not is_unknown_state(meta) or now_ms - last_unknown_saved_ms < UNKNOWN_SAVE_INTERVAL_MS:
        return

    day_dir = UNKNOWN_DIR / time.strftime("%Y%m%d", time.localtime(now_ms / 1000))
    day_dir.mkdir(parents=True, exist_ok=True)
    filename = "unknown_{}_seq{}_faces{}_users{}.jpg".format(
        time.strftime("%Y%m%d_%H%M%S", time.localtime(now_ms / 1000)),
        meta.get("seq", 0),
        meta.get("faces", 0),
        meta.get("users", 0),
    )
    (day_dir / filename).write_bytes(frame)
    last_unknown_saved_ms = now_ms
    unknown_saved_count += 1


def list_unknown_files():
    cleanup_unknown_faces()
    files = []
    if not UNKNOWN_DIR.exists():
        return files
    for path in sorted(UNKNOWN_DIR.rglob("*.jpg"), key=lambda p: p.stat().st_mtime, reverse=True)[:200]:
        rel = path.relative_to(UNKNOWN_DIR).as_posix()
        files.append({
            "name": path.name,
            "rel": rel,
            "url": "/api/unknown-image/" + quote(rel),
            "size": path.stat().st_size,
            "mtime": int(path.stat().st_mtime * 1000),
        })
    return files


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        return

    def is_viewer_authorized(self):
        if not VIEWER_PASSWORD:
            return True
        cookie = self.headers.get("Cookie", "")
        for part in cookie.split(";"):
            name, _, value = part.strip().partition("=")
            if name == SESSION_COOKIE_NAME and value == SESSION_TOKEN:
                return True
        auth = self.headers.get("Authorization", "")
        prefix = "Basic "
        if not auth.startswith(prefix):
            return False
        try:
            decoded = base64.b64decode(auth[len(prefix):]).decode("utf-8")
        except Exception:
            return False
        return decoded == f"{VIEWER_USERNAME}:{VIEWER_PASSWORD}"

    def require_viewer_auth(self):
        if self.is_viewer_authorized():
            return True
        self.send_body(200, "text/html; charset=utf-8", LOGIN_PAGE.replace("__ERROR__", ""))
        return False

    def send_body(self, status, content_type, body):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_HEAD(self):
        if not self.require_viewer_auth():
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

    def do_GET(self):
        global latest_frame, latest_meta
        if not self.require_viewer_auth():
            return
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self.send_body(200, "text/html; charset=utf-8", PAGE)
        elif parsed.path == "/api/latest.jpg":
            if latest_frame is None:
                self.send_body(404, "text/plain", "no frame yet")
            else:
                self.send_body(200, "image/jpeg", latest_frame)
        elif parsed.path == "/api/status":
            meta = dict(latest_meta)
            meta["ageMs"] = int(time.time() * 1000) - meta["updatedAt"] if meta.get("updatedAt") else None
            meta["unknownSaved"] = unknown_saved_count
            self.send_body(200, "application/json", json.dumps(meta))
        elif parsed.path == "/api/unknown":
            self.send_body(200, "application/json", json.dumps({"files": list_unknown_files()}))
        elif parsed.path.startswith("/api/unknown-image/"):
            rel = unquote(parsed.path[len("/api/unknown-image/"):])
            path = (UNKNOWN_DIR / rel).resolve()
            root = UNKNOWN_DIR.resolve()
            if root not in path.parents or not path.is_file() or path.suffix.lower() != ".jpg":
                self.send_body(404, "text/plain", "not found")
            else:
                self.send_body(200, "image/jpeg", path.read_bytes())
        else:
            self.send_body(404, "text/plain", "not found")

    def do_POST(self):
        global latest_frame, latest_meta, unknown_saved_count
        parsed = urlparse(self.path)
        if parsed.path == "/login":
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(min(length, 4096)).decode("utf-8", errors="replace")
            form = parse_qs(body)
            username = form.get("username", [""])[0]
            password = form.get("password", [""])[0]
            if username == VIEWER_USERNAME and password == VIEWER_PASSWORD:
                self.send_response(302)
                self.send_header("Location", "/")
                self.send_header(
                    "Set-Cookie",
                    f"{SESSION_COOKIE_NAME}={SESSION_TOKEN}; Path=/; Max-Age=86400; HttpOnly; SameSite=Lax",
                )
                self.end_headers()
            else:
                self.send_body(200, "text/html; charset=utf-8", LOGIN_PAGE.replace("__ERROR__", "Invalid username or password"))
            return

        if parsed.path != "/api/frame":
            self.send_body(404, "text/plain", "not found")
            return
        if DEVICE_TOKEN and self.headers.get("X-Device-Token") != DEVICE_TOKEN:
            self.send_body(401, "text/plain", "unauthorized")
            return

        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 512 * 1024:
            self.send_body(413, "text/plain", "invalid frame size")
            return

        latest_frame = self.rfile.read(length)
        query = parse_qs(parsed.query)
        latest_meta = {
            "state": query.get("state", ["UNKNOWN"])[0],
            "raw": query.get("raw", ["UNKNOWN"])[0],
            "faces": int(query.get("faces", ["0"])[0]),
            "users": int(query.get("users", ["0"])[0]),
            "seq": int(query.get("seq", ["0"])[0]),
            "updatedAt": int(time.time() * 1000),
            "unknownSaved": unknown_saved_count,
        }
        save_unknown_frame(latest_frame, latest_meta)
        latest_meta["unknownSaved"] = unknown_saved_count
        self.send_response(204)
        self.end_headers()


if __name__ == "__main__":
    UNKNOWN_DIR.mkdir(parents=True, exist_ok=True)
    removed = cleanup_unknown_faces(force=True)
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"ESP32-S3 viewer listening on http://{HOST}:{PORT}", flush=True)
    print(f"Viewer username: {VIEWER_USERNAME}", flush=True)
    print(f"Unknown frames directory: {UNKNOWN_DIR}", flush=True)
    print(f"Unknown retention days: {UNKNOWN_RETENTION_DAYS}, removed on startup: {removed}", flush=True)
    server.serve_forever()
