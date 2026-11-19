# ESP Rotary Hub (IDEAQUX / Elecrow CrowPanel 1.46" ESP32-S3)

Turns your USB-connected **IDEAQUX / Elecrow CrowPanel 1.46" HMI ESP32-S3 Rotary Display** (`360×360` ST77961 + CST816T/D touch + quadrature knob + 8× WS2812 RGB LEDs) into:

1. **macOS "Now Playing" Media & Volume Controller** — live track title, artist, source app, progress bar, circular volume arc, Play/Pause, Next/Prev, and system volume synced with macOS via [`media-control`](https://github.com/ungive/media-control).
2. **Universal Coding-Agent Notification & Interactive Approval Knob** — real-time status & **Approve / Deny / Focus IDE** prompts for **Antigravity**, **Claude Code (VS Code / CLI)**, and any other tool via a local HTTP bridge (`http://127.0.0.1:48912`).

---

## Controls

| View | Action | Behavior |
| :--- | :--- | :--- |
| **Any Main View** | **Swipe Left / Right** or **Long-press knob (0.6s)** | Switch between **Media** and **Agent** views |
| **Media View** | **Rotate knob** | Adjust macOS system volume (±3% per detent) |
| **Media View** | **Single click** (or tap center button) | Play / Pause |
| **Media View** | **Double click** (or tap `>` / `<`) | Next / Previous track |
| **Agent View** | **Single click** (or tap `FOCUS IDE`) | Bring Antigravity or VS Code to the foreground |
| **Approval Popup** | **Rotate knob** | Cycle between **ALLOW**, **DENY**, and **IDE** (focus & ask in IDE) |
| **Approval Popup** | **Single click** (or tap pill directly) | Send decision immediately back to the coding agent |

### Ambient WS2812 LED Ring
* **Amber pulse**: Coding agent waiting for your approval
* **Blue breathing**: Agent working
* **Green**: Agent finished turn
* **Red**: Agent / tool error
* **Soft cyan**: Media playing

---

## Quick Start

### 1. Build & Flash Firmware (PlatformIO)
```bash
uvx --from platformio --with pip-system-certs pio run -d firmware
uvx --from platformio --with pip-system-certs pio run -d firmware -t upload
```

### 2. Start the macOS Companion Daemon
Requires [`media-control`](https://github.com/ungive/media-control) (`brew tap ungive/media-control && brew install media-control`):
```bash
uvx --with pyserial --with pillow python3 companion/rotary_daemon.py
```
The daemon auto-detects the CrowPanel on `/dev/cu.usbmodem*`, reconnects automatically, streams macOS Now Playing + volume, and listens on `http://127.0.0.1:48912`.

### 3. Enable Coding-Agent Hooks

* **Antigravity**: Copy `hooks/antigravity-hooks.json` to `~/.gemini/config/hooks.json` (global) or `.agents/hooks.json` (per-project).
* **Claude Code (VS Code / CLI)**: Merge `hooks/claude-code-settings.json` into `~/.claude/settings.json`.
* **Any other tool / script**:
  ```bash
  ./hooks/notify.sh vscode done "Build Passed" "All tests green"
  ```

---

## Project Layout

* `firmware/` — PlatformIO ESP32-S3 firmware (LovyanGFX, LVGL 8.3, CST816D, Adafruit NeoPixel, ArduinoJson)
* `companion/rotary_daemon.py` — macOS USB serial + Now Playing + Volume + Local HTTP Agent Bridge
* `hooks/` — Antigravity, Claude Code, and generic shell hook integrations
* `references/` — Cloned upstream references (`crowpanel-1.28`, `crowpanel-esphome`, `blueknob`)
