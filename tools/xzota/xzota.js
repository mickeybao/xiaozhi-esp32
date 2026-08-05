import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url));
const CONFIG_FILE = path.join(SCRIPT_DIR, "xzota-config.txt");

const DEFAULT_CONFIG = {
  PORT: "8081",
  FIRMWARE_VERSION: "2.2.10",
  FIRMWARE_FILE: "xiaozhi.bin",
  FIRMWARE_URL: "",
  ENABLE_FIRMWARE_UPGRADE: "0",
  ASSETS_VERSION: "1.0.0",
  ASSETS_FILE: "expression_assets.bin",
  ASSETS_URL: "",
  ENABLE_ASSETS_UPGRADE: "0",
};

function readTextConfig(filePath) {
  if (!fs.existsSync(filePath)) {
    return { ...DEFAULT_CONFIG };
  }

  const config = { ...DEFAULT_CONFIG };
  const text = fs.readFileSync(filePath, "utf8").replace(/^\uFEFF/, "");

  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim();
    if (!line || line.startsWith("#") || line.startsWith(";")) {
      continue;
    }

    const separatorIndex = line.indexOf("=");
    if (separatorIndex === -1) {
      continue;
    }

    const key = line.slice(0, separatorIndex).trim().replace(/^\uFEFF/, "");
    let value = line.slice(separatorIndex + 1).trim();

    if (
      (value.startsWith('"') && value.endsWith('"')) ||
      (value.startsWith("'") && value.endsWith("'"))
    ) {
      value = value.slice(1, -1);
    }

    if (key) {
      config[key] = value;
    }
  }

  return config;
}

const OFFICIAL_OTA_URL = "https://api.tenclass.net/xiaozhi/ota/";

function getRuntimeConfig() {
  const config = readTextConfig(CONFIG_FILE);
  const firmwareVersion = config.FIRMWARE_VERSION || "2.2.10";
  const firmwareFile = config.FIRMWARE_FILE || "xiaozhi.bin";
  const firmwareFilePath = path.isAbsolute(firmwareFile)
    ? firmwareFile
    : path.join(SCRIPT_DIR, firmwareFile);
  const firmwareDownloadName = path.basename(firmwareFile.replace(/\\/g, "/"));
  const firmwareUrl =
    config.FIRMWARE_URL || `http://xz.atchain.cn/${firmwareDownloadName}`;
  const enableFirmwareUpgrade = ["1", "true", "yes", "on"].includes(
    String(config.ENABLE_FIRMWARE_UPGRADE || "0").trim().toLowerCase()
  );
  const assetsVersion = config.ASSETS_VERSION || "1.0.0";
  const assetsFile = config.ASSETS_FILE || "expression_assets.bin";
  const assetsFilePath = path.isAbsolute(assetsFile)
    ? assetsFile
    : path.join(SCRIPT_DIR, assetsFile);
  const assetsDownloadName = path.basename(assetsFile.replace(/\\/g, "/"));
  const assetsUrl =
    config.ASSETS_URL || `http://xz.atchain.cn/${assetsDownloadName}`;
  const enableAssetsUpgrade = ["1", "true", "yes", "on"].includes(
    String(config.ENABLE_ASSETS_UPGRADE || "0").trim().toLowerCase()
  );

  return {
    port: Number(config.PORT || 8081),
    firmwareVersion,
    firmwareFile,
    firmwareFilePath,
    firmwareDownloadName,
    firmwareUrl,
    enableFirmwareUpgrade,
    assetsVersion,
    assetsFile,
    assetsFilePath,
    assetsDownloadName,
    assetsUrl,
    enableAssetsUpgrade,
  };
}

const PORT = getRuntimeConfig().port;

function getFirmwareFileInfo(runtimeConfig) {
  return getBinaryFileInfo(runtimeConfig.firmwareFilePath);
}

function getAssetsFileInfo(runtimeConfig) {
  return getBinaryFileInfo(runtimeConfig.assetsFilePath);
}

function getBinaryFileInfo(filePath) {
  if (!fs.existsSync(filePath)) {
    return {
      exists: false,
      path: filePath,
      size: 0,
    };
  }

  const stat = fs.statSync(filePath);
  return {
    exists: true,
    path: filePath,
    size: stat.size,
    mtime: stat.mtime.toISOString(),
  };
}

function getEspAppDesc(filePath) {
  if (!fs.existsSync(filePath)) {
    return null;
  }

  const data = fs.readFileSync(filePath);
  const offset = 0x18 + 8;
  if (data.length < offset + 0x70) {
    return null;
  }

  if (data.readUInt32LE(offset) !== 0xabcd5432) {
    return null;
  }

  const readString = (start, length) =>
    data
      .subarray(offset + start, offset + start + length)
      .toString("utf8")
      .replace(/\0.*$/, "");

  return {
    project: readString(0x30, 0x20),
    version: readString(0x10, 0x20),
    compile_time: `${readString(0x60, 0x10)}T${readString(0x50, 0x10)}`,
  };
}

function sendJson(res, statusCode, data) {
  const body = JSON.stringify(data);
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
  });
  res.end(body);
}

function sendFile(req, res, filePath) {
  const fullPath = path.resolve(filePath);

  if (!fs.existsSync(fullPath)) {
    console.error("Binary file not found:", fullPath);
    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    res.end(`File not found: ${filePath}`);
    return;
  }

  const stat = fs.statSync(fullPath);
  console.log("Binary download:", {
    method: req.method,
    url: req.url,
    file: fullPath,
    size: stat.size,
    user_agent: req.headers["user-agent"],
  });

  res.writeHead(200, {
    "Content-Type": "application/octet-stream",
    "Content-Length": stat.size,
    "Cache-Control": "no-store",
    "Accept-Ranges": "none",
    "Connection": "close",
  });

  if (req.method === "HEAD") {
    res.end();
    return;
  }

  const stream = fs.createReadStream(fullPath);
  stream.on("error", (error) => {
    console.error("Binary stream error:", error);
    if (!res.headersSent) {
      res.writeHead(500, { "Content-Type": "text/plain; charset=utf-8" });
    }
    res.end("Binary stream error");
  });
  stream.pipe(res);
}

function readRequestBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];

    req.on("data", (chunk) => chunks.push(chunk));
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

function copyDeviceHeaders(req) {
  const headers = {
    "Content-Type": "application/json",
  };

  const passHeaders = [
    "activation-version",
    "device-id",
    "client-id",
    "serial-number",
    "user-agent",
    "accept-language",
  ];

  for (const name of passHeaders) {
    const value = req.headers[name];
    if (value) {
      headers[name] = value;
    }
  }

  return headers;
}

async function proxyOfficialOta(req, bodyBuffer) {
  const response = await fetch(OFFICIAL_OTA_URL, {
    method: bodyBuffer.length > 0 ? "POST" : "GET",
    headers: copyDeviceHeaders(req),
    body: bodyBuffer.length > 0 ? bodyBuffer : undefined,
  });

  const text = await response.text();

  let data;
  try {
    data = JSON.parse(text);
  } catch {
    throw new Error(`Official OTA returned non-JSON: ${text.slice(0, 200)}`);
  }

  if (!response.ok) {
    const message = data?.message || data?.error || response.statusText;
    throw new Error(`Official OTA failed: ${response.status} ${message}`);
  }

  return data;
}

function patchFirmwareSection(data, runtimeConfig) {
  if (!runtimeConfig.enableFirmwareUpgrade) {
    delete data.firmware;
    return data;
  }

  data.firmware = {
    version: runtimeConfig.firmwareVersion,
    url: runtimeConfig.firmwareUrl,
  };

  return data;
}

function patchAssetsSection(data, runtimeConfig) {
  if (!runtimeConfig.enableAssetsUpgrade) {
    delete data.assets;
    return data;
  }

  data.assets = {
    version: runtimeConfig.assetsVersion,
    url: runtimeConfig.assetsUrl,
  };

  return data;
}

function addDebugInfo(data, runtimeConfig) {
  data.proxy = {
    name: "xiaozhi-ota-proxy",
    version: 27,
    official: OFFICIAL_OTA_URL,
    config_file: CONFIG_FILE,
    firmware_upgrade_enabled: runtimeConfig.enableFirmwareUpgrade,
    assets_upgrade_enabled: runtimeConfig.enableAssetsUpgrade,
  };

  return data;
}

async function handleOta(req, res, runtimeConfig) {
  try {
    const bodyBuffer = await readRequestBody(req);
    const officialData = await proxyOfficialOta(req, bodyBuffer);

    const result = addDebugInfo(
      patchAssetsSection(patchFirmwareSection(officialData, runtimeConfig), runtimeConfig),
      runtimeConfig
    );

    console.log("OTA response:", {
      firmware: result.firmware,
      assets: result.assets,
      firmware_file: getFirmwareFileInfo(runtimeConfig),
      firmware_app: getEspAppDesc(runtimeConfig.firmwareFilePath),
      assets_file: getAssetsFileInfo(runtimeConfig),
      has_mqtt: Boolean(result.mqtt),
      has_websocket: Boolean(result.websocket),
      device_id: req.headers["device-id"],
      user_agent: req.headers["user-agent"],
    });

    sendJson(res, 200, result);
  } catch (error) {
    console.error("OTA proxy error:", error);

    sendJson(res, 500, {
      error: "ota_proxy_failed",
      message: error.message,
      firmware: undefined,
      assets: runtimeConfig.enableAssetsUpgrade
        ? {
            version: runtimeConfig.assetsVersion,
            url: runtimeConfig.assetsUrl,
          }
        : undefined,
    });
  }
}

const server = http.createServer(async (req, res) => {
  const runtimeConfig = getRuntimeConfig();
  const url = new URL(req.url, `http://${req.headers.host}`);

  if (url.pathname === "/ota/" || url.pathname === "/ota") {
    await handleOta(req, res, runtimeConfig);
    return;
  }

  if (url.pathname === `/${runtimeConfig.firmwareDownloadName}`) {
    sendFile(req, res, runtimeConfig.firmwareFilePath);
    return;
  }

  if (url.pathname === `/${runtimeConfig.assetsDownloadName}`) {
    sendFile(req, res, runtimeConfig.assetsFilePath);
    return;
  }

  if (url.pathname === "/health") {
    const firmwareFile = getFirmwareFileInfo(runtimeConfig);
    const assetsFile = getAssetsFileInfo(runtimeConfig);
    sendJson(res, 200, {
      status: "ok",
      version: 27,
      config_file: CONFIG_FILE,
      port: runtimeConfig.port,
      port_note: "PORT changes require restarting node because the server socket is opened at startup.",
      firmware_version: runtimeConfig.firmwareVersion,
      firmware_upgrade_enabled: runtimeConfig.enableFirmwareUpgrade,
      firmware_url: runtimeConfig.firmwareUrl,
      firmware_download_path: `/${runtimeConfig.firmwareDownloadName}`,
      firmware_file: firmwareFile,
      firmware_app: getEspAppDesc(runtimeConfig.firmwareFilePath),
      assets_version: runtimeConfig.assetsVersion,
      assets_upgrade_enabled: runtimeConfig.enableAssetsUpgrade,
      assets_url: runtimeConfig.assetsUrl,
      assets_download_path: `/${runtimeConfig.assetsDownloadName}`,
      assets_file: assetsFile,
    });
    return;
  }

  sendJson(res, 404, {
    error: "not_found",
    paths: [
      "/ota/",
      "/health",
      `/${runtimeConfig.firmwareDownloadName}`,
      `/${runtimeConfig.assetsDownloadName}`,
    ],
  });
});

server.listen(PORT, "0.0.0.0", () => {
  const runtimeConfig = getRuntimeConfig();
  console.log(`Xiaozhi OTA proxy v27 listening on http://0.0.0.0:${PORT}/ota/`);
  console.log(`Config file: ${CONFIG_FILE}`);
  console.log("Config is reloaded on every request except PORT.");
  console.log(`Firmware upgrade enabled: ${runtimeConfig.enableFirmwareUpgrade}`);
  console.log(`Firmware version: ${runtimeConfig.firmwareVersion}`);
  console.log(`Firmware file: ${runtimeConfig.firmwareFilePath}`);
  console.log(`Firmware file info:`, getFirmwareFileInfo(runtimeConfig));
  console.log(`Firmware URL: ${runtimeConfig.firmwareUrl}`);
  console.log(`Assets upgrade enabled: ${runtimeConfig.enableAssetsUpgrade}`);
  console.log(`Assets version: ${runtimeConfig.assetsVersion}`);
  console.log(`Assets file: ${runtimeConfig.assetsFilePath}`);
  console.log(`Assets file info:`, getAssetsFileInfo(runtimeConfig));
  console.log(`Assets URL: ${runtimeConfig.assetsUrl}`);
});
