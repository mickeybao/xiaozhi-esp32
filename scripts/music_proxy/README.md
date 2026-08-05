# XiaoZhi Music Proxy

This small local service converts an HTTP audio URL into an Ogg Opus stream that the existing `self.radio.play` firmware tool can play.

It is intentionally dependency-light: Python standard library plus `ffmpeg`.

## Install ffmpeg

Windows examples:

```powershell
winget install Gyan.FFmpeg
```

or install ffmpeg manually and add `ffmpeg.exe` to `PATH`.

If `/health` shows a relative path such as `.\ffmpeg.EXE`, make sure it is not a shim/wrapper. On Windows, prefer passing the real binary explicitly:

```powershell
python scripts\music_proxy\server.py --host 0.0.0.0 --port 8765 --ffmpeg C:\ffmpeg\bin\ffmpeg.exe
```

Ubuntu/Debian server:

```bash
sudo apt update
sudo apt install -y ffmpeg python3
```

## Run

```powershell
python scripts\music_proxy\server.py --host 0.0.0.0 --port 8765
```

Check:

```text
http://<computer-ip>:8765/health
```

Expected `ffmpeg_check.ok`:

```json
true
```

## Serve a Local Test MP3

Put test audio files under:

```text
scripts/music_proxy/files/
```

For example:

```text
scripts/music_proxy/files/1.mp3
```

Then access the raw MP3 file through:

```text
http://<computer-ip>:8765/download?file=1.mp3
```

or the query-free form:

```text
http://<computer-ip>:8765/download/1.mp3
```

This is useful for simulating an external MP3 URL before the NetEase Cloud Music resolver is ready.

## Stream a direct MP3/AAC URL

URL-encode the source audio URL, then ask XiaoZhi to play:

```text
http://<computer-ip>:8765/stream?url=https%3A%2F%2Fexample.com%2Fsong.mp3
```

`url=` is parsed as "everything after this point", so it can also carry third-party URLs with their own `?` and `&` query parameters. Keep `url=` as the final music-proxy parameter:

```text
http://<computer-ip>:8765/stream?url=https://example.com/song.mp3?token=abc&expires=123
```

To verify what the server will pass to ffmpeg:

```text
http://<computer-ip>:8765/debug/stream-url?url=https://example.com/song.mp3?token=abc&expires=123
```

For a local test file, URL-encode the `/download` URL:

```text
http://<computer-ip>:8765/stream?url=http%3A%2F%2F<computer-ip>%3A8765%2Fdownload%3Ffile%3D1.mp3
```

For quick testing, prefer transcoding the local file directly:

```text
http://<computer-ip>:8765/stream?file=1.mp3
```

The query-free download URL is also safe to use without encoding:

```text
http://<computer-ip>:8765/stream?url=http://<computer-ip>:8765/download/1.mp3
```

Internally this runs ffmpeg roughly like:

```text
ffmpeg -i <source-url> -vn -c:a libopus -b:a 24k -ac 1 -ar 16000 -frame_duration 60 -f ogg pipe:1
```

The firmware side can reuse the existing radio player:

```json
{"name":"self.radio.play","arguments":{"name":"测试歌曲","url":"http://<computer-ip>:8765/stream?url=..."}}
```

## Optional local catalog

Copy the example catalog and replace entries with playable URLs you are allowed to use:

```powershell
Copy-Item scripts\music_proxy\catalog.example.json scripts\music_proxy\catalog.json
```

Search:

```text
http://<computer-ip>:8765/music/search?q=demo
```

Play:

```text
http://<computer-ip>:8765/music/play?id=demo-1
```

Later, NetEase Cloud Music integration should replace the catalog lookup with an authenticated, rights-respecting resolver that returns a playable source URL for the requested song. Keep the device-facing output as Ogg Opus so no firmware audio decoder change is needed.

## Server Deployment

On a Linux server, put this repository somewhere stable, for example:

```bash
cd /opt/xiaozhi-esp32
python3 scripts/music_proxy/server.py --host 0.0.0.0 --port 8765
```

For a long-running systemd service, create `/etc/systemd/system/xiaozhi-music-proxy.service`:

```ini
[Unit]
Description=XiaoZhi Music Proxy
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/xiaozhi-esp32
ExecStart=/usr/bin/python3 /opt/xiaozhi-esp32/scripts/music_proxy/server.py --host 0.0.0.0 --port 8765
Restart=always
RestartSec=3
User=www-data
Group=www-data

[Install]
WantedBy=multi-user.target
```

Then:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now xiaozhi-music-proxy
sudo systemctl status xiaozhi-music-proxy
```

If you put Nginx or Cloudflare in front of it, make sure streaming responses are not buffered. For Nginx, the location should include:

```nginx
proxy_buffering off;
proxy_request_buffering off;
proxy_read_timeout 3600s;
```

The XiaoZhi device should play the public server URL, for example:

```text
https://music.example.com/stream?url=...
```
