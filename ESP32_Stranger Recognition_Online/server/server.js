const http = require("http");
const { URL } = require("url");

const host = process.env.HOST || "0.0.0.0";
const port = Number(process.env.PORT || 8080);
const deviceToken = process.env.DEVICE_TOKEN || "";

let latestFrame = null;
let latestMeta = {
  state: "WAITING_FOR_FRAME",
  raw: "WAITING_FOR_FRAME",
  faces: 0,
  users: 0,
  seq: 0,
  ageMs: null,
  updatedAt: null,
};

const page = `<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Face Guard</title>
  <style>
    :root {
      color-scheme: dark;
      font-family: Arial, "Microsoft YaHei", sans-serif;
      background: #101418;
      color: #f5f7fa;
    }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      grid-template-rows: auto 1fr;
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      padding: 14px 18px;
      background: #171d24;
      border-bottom: 1px solid #2a323c;
    }
    h1 {
      margin: 0;
      font-size: 18px;
      font-weight: 700;
    }
    main {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 280px;
      gap: 18px;
      padding: 18px;
      box-sizing: border-box;
    }
    .viewer {
      min-width: 0;
      display: grid;
      place-items: center;
      background: #080b0e;
      border: 1px solid #2a323c;
      border-radius: 8px;
      overflow: hidden;
    }
    #frame {
      width: min(100%, 820px);
      image-rendering: auto;
      aspect-ratio: 1 / 1;
      object-fit: contain;
      display: block;
    }
    aside {
      display: grid;
      align-content: start;
      gap: 10px;
    }
    .metric {
      border: 1px solid #2a323c;
      border-radius: 8px;
      padding: 12px;
      background: #171d24;
    }
    .label {
      color: #9fb0c3;
      font-size: 12px;
      margin-bottom: 6px;
    }
    .value {
      font-size: 18px;
      font-weight: 700;
      overflow-wrap: anywhere;
    }
    .danger { color: #ff6868; }
    .ok { color: #6ee7a8; }
    @media (max-width: 760px) {
      main { grid-template-columns: 1fr; }
      aside { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
  </style>
</head>
<body>
  <header>
    <h1>ESP32-S3 Face Guard</h1>
    <span id="status">connecting</span>
  </header>
  <main>
    <section class="viewer">
      <img id="frame" alt="latest camera frame">
    </section>
    <aside>
      <div class="metric"><div class="label">stable result</div><div class="value" id="state">-</div></div>
      <div class="metric"><div class="label">raw result</div><div class="value" id="raw">-</div></div>
      <div class="metric"><div class="label">faces</div><div class="value" id="faces">0</div></div>
      <div class="metric"><div class="label">registered users</div><div class="value" id="users">0</div></div>
      <div class="metric"><div class="label">frame seq</div><div class="value" id="seq">0</div></div>
      <div class="metric"><div class="label">age</div><div class="value" id="age">-</div></div>
    </aside>
  </main>
  <script>
    const frame = document.getElementById("frame");
    const ids = ["state", "raw", "faces", "users", "seq", "age"];
    const setText = (id, value) => document.getElementById(id).textContent = value;

    async function refreshMeta() {
      try {
        const res = await fetch("/api/status", { cache: "no-store" });
        const meta = await res.json();
        ids.forEach((id) => {
          if (id === "age") {
            setText(id, meta.ageMs == null ? "-" : Math.round(meta.ageMs) + " ms");
          } else {
            setText(id, meta[id] ?? "-");
          }
        });
        document.getElementById("state").className =
          "value " + (String(meta.state).includes("UNKNOWN") ? "danger" : "ok");
        document.getElementById("status").textContent = meta.updatedAt ? "online" : "waiting";
      } catch {
        document.getElementById("status").textContent = "server unavailable";
      }
    }

    function refreshFrame() {
      frame.src = "/api/latest.jpg?t=" + Date.now();
    }

    setInterval(refreshMeta, 500);
    setInterval(refreshFrame, 250);
    refreshMeta();
    refreshFrame();
  </script>
</body>
</html>`;

function send(res, statusCode, headers, body) {
  res.writeHead(statusCode, headers);
  res.end(body);
}

function handleUpload(req, res, url) {
  if (deviceToken && req.headers["x-device-token"] !== deviceToken) {
    send(res, 401, { "Content-Type": "text/plain" }, "unauthorized");
    return;
  }

  const chunks = [];
  let total = 0;
  req.on("data", (chunk) => {
    total += chunk.length;
    if (total <= 512 * 1024) {
      chunks.push(chunk);
    } else {
      req.destroy();
    }
  });
  req.on("end", () => {
    latestFrame = Buffer.concat(chunks);
    latestMeta = {
      state: url.searchParams.get("state") || "UNKNOWN",
      raw: url.searchParams.get("raw") || "UNKNOWN",
      faces: Number(url.searchParams.get("faces") || 0),
      users: Number(url.searchParams.get("users") || 0),
      seq: Number(url.searchParams.get("seq") || 0),
      updatedAt: Date.now(),
    };
    send(res, 204, {}, "");
  });
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  if (req.method === "GET" && url.pathname === "/") {
    send(res, 200, { "Content-Type": "text/html; charset=utf-8" }, page);
  } else if (req.method === "POST" && url.pathname === "/api/frame") {
    handleUpload(req, res, url);
  } else if (req.method === "GET" && url.pathname === "/api/latest.jpg") {
    if (!latestFrame) {
      send(res, 404, { "Content-Type": "text/plain" }, "no frame yet");
    } else {
      send(res, 200, { "Content-Type": "image/jpeg", "Cache-Control": "no-store" }, latestFrame);
    }
  } else if (req.method === "GET" && url.pathname === "/api/status") {
    const body = JSON.stringify({
      ...latestMeta,
      ageMs: latestMeta.updatedAt ? Date.now() - latestMeta.updatedAt : null,
    });
    send(res, 200, { "Content-Type": "application/json", "Cache-Control": "no-store" }, body);
  } else {
    send(res, 404, { "Content-Type": "text/plain" }, "not found");
  }
});

server.listen(port, host, () => {
  console.log(`ESP32-S3 viewer listening on http://${host}:${port}`);
});
