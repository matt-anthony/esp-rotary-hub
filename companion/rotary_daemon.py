#!/usr/bin/env python3
"""
macOS Companion Daemon for ESP Rotary Hub (Elecrow CrowPanel 1.28").

Responsibilities:
1. Auto-detects and maintains a USB serial connection to the CrowPanel ESP32-S3.
2. Streams macOS "Now Playing" metadata (via `media-control`) and system volume
   (via `osascript`) to the rotary display.
3. Executes media & volume commands sent back from the knob/touchscreen.
4. Hosts a local HTTP bridge on http://127.0.0.1:48912 so coding agents
   (Antigravity, Claude Code in VS Code, Cursor, shell scripts, etc.) can push
   status updates and request interactive knob approvals.
"""

from __future__ import annotations

import argparse
import base64
import ctypes
import glob
import hashlib
import http.server
import io
import json
import os
import re
import shutil
import socketserver
import struct
import subprocess
import sys
import threading
import time
import uuid
from typing import Any, Dict, Optional

import serial
import serial.tools.list_ports

try:
    from PIL import Image, ImageEnhance
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

ART_SIZE = 120
ART_BYTES = ART_SIZE * ART_SIZE * 2
ART_CHUNK_RAW = 512

HOST = "127.0.0.1"
PORT = 48912
BAUD = 115200
VOL_STEP = 3  # % per rotary detent

BUNDLE_APP_MAP = {
    "com.spotify.client": "Spotify",
    "com.apple.Music": "Apple Music",
    "com.apple.TV": "Apple TV",
    "com.apple.Safari": "Safari",
    "com.google.Chrome": "Chrome",
    "company.thebrowser.Browser": "Arc",
    "com.brave.Browser": "Brave",
    "org.mozilla.firefox": "Firefox",
    "com.apple.QuickTimePlayerX": "QuickTime",
    "org.videolan.vlc": "VLC",
    "com.tidal.desktop": "TIDAL",
    "tv.plex.desktop": "Plex",
}

PLATFORM_MAC_APP = {
    "antigravity": "Antigravity",
    "claude-code": "Visual Studio Code",
    "vscode": "Visual Studio Code",
    "cursor": "Cursor",
    "terminal": "Terminal",
}


def sanitize_ascii(text: str, max_len: int = 80) -> str:
    """Strip emojis/non-ASCII that Montserrat bitmap font cannot render."""
    if not text:
        return ""
    cleaned = text.encode("ascii", "ignore").decode("ascii")
    cleaned = re.sub(r"\s+", " ", cleaned).strip()
    if len(cleaned) > max_len:
        cleaned = cleaned[: max_len - 1].rstrip() + "~"
    return cleaned


def friendly_app_name(bundle_id: str) -> str:
    if not bundle_id:
        return "MAC MEDIA"
    if bundle_id in BUNDLE_APP_MAP:
        return BUNDLE_APP_MAP[bundle_id]
    parts = bundle_id.split(".")
    return parts[-1].capitalize() if parts else "MAC MEDIA"


def process_artwork_rgb565(b64_data: str) -> tuple[Optional[bytes], list[int]]:
    """Decode base64 artwork -> 180x180 dimmed RGB565 bytes + dominant accent RGB."""
    default_rgb = [0, 210, 240]
    if not HAS_PIL or not b64_data:
        return None, default_rgb
    try:
        raw = base64.b64decode(b64_data)
        img = Image.open(io.BytesIO(raw)).convert("RGB")
        # Center-crop to square
        w, h = img.size
        side = min(w, h)
        left = (w - side) // 2
        top = (h - side) // 2
        img = img.crop((left, top, left + side, top + side))

        # Extract vibrant accent color for the 8x WS2812 LED ring
        tiny = img.resize((16, 16), Image.Resampling.BILINEAR)
        pixels = list(tiny.get_flattened_data() if hasattr(tiny, "get_flattened_data") else tiny.getdata())
        best_rgb = default_rgb
        best_score = -1.0
        for r, g, b in pixels:
            mx, mn = max(r, g, b), min(r, g, b)
            sat = (mx - mn) / 255.0
            score = sat * (mx / 255.0)
            if score > best_score and mx > 50:
                best_score = score
                best_rgb = [r, g, b]

        # Resize to 180x180 and dim to ~48% so white text & cyan arc pop cleanly
        img = img.resize((ART_SIZE, ART_SIZE), Image.Resampling.LANCZOS)
        img = ImageEnhance.Brightness(img).enhance(0.48)

        # Pack to little-endian RGB565 (LV_COLOR_16_SWAP = 0)
        rgb_bytes = img.tobytes()
        out = bytearray(ART_BYTES)
        idx = 0
        for i in range(0, len(rgb_bytes), 3):
            r = rgb_bytes[i]
            g = rgb_bytes[i + 1]
            b = rgb_bytes[i + 2]
            c565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[idx] = c565 & 0xFF
            out[idx + 1] = (c565 >> 8) & 0xFF
            idx += 2
        return bytes(out), best_rgb
    except Exception as e:
        print(f"[art] decode error: {e}", file=sys.stderr)
        return None, default_rgb


def get_mac_volume() -> tuple[int, bool]:
    try:
        out = subprocess.check_output(
            ["osascript", "-e", "get volume settings"],
            text=True,
            timeout=1.5,
        ).strip()
        vol_m = re.search(r"output volume:(\d+)", out)
        mute_m = re.search(r"output muted:(true|false)", out)
        vol = int(vol_m.group(1)) if vol_m else 35
        muted = (mute_m.group(1) == "true") if mute_m else False
        return vol, muted
    except Exception:
        return 35, False


def set_mac_volume(vol: int) -> None:
    vol = max(0, min(100, vol))
    try:
        subprocess.Popen(
            ["osascript", "-e", f"set volume output volume {vol}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        pass


def run_media_cmd(cmd: str) -> None:
    mc = shutil.which("media-control")
    if mc:
        mapping = {
            "toggle": "toggle-play-pause",
            "play": "play",
            "pause": "pause",
            "next": "next-track",
            "prev": "previous-track",
        }
        sub = mapping.get(cmd)
        if sub:
            subprocess.Popen(
                [mc, sub],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            return
    # Fallback via AppleScript for Apple Music / Spotify if media-control missing
    if cmd == "toggle":
        subprocess.Popen(
            ["osascript", "-e", 'tell application "System Events" to key code 16'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def focus_mac_app(platform: str) -> None:
    app_name = PLATFORM_MAC_APP.get(platform.lower(), "Antigravity")
    try:
        subprocess.Popen(
            ["open", "-a", app_name],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        pass


class _CGPoint(ctypes.Structure):
    _fields_ = [("x", ctypes.c_double), ("y", ctypes.c_double)]


class MacBlenderController:
    """Synthesizes macOS Middle-Mouse Orbit/Pan, Scroll Zoom, and Numpad-. Frame."""

    kCGHIDEventTap = 0
    kCGMouseButtonCenter = 2
    kCGEventOtherMouseDown = 25
    kCGEventOtherMouseUp = 26
    kCGEventOtherMouseDragged = 27
    kCGScrollEventUnitPixel = 0
    kCGEventFlagMaskShift = 0x00020000
    kVK_ANSI_KeypadDecimal = 0x41

    def __init__(self) -> None:
        self.cg = ctypes.cdll.LoadLibrary(
            "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics"
        )
        self.cf = ctypes.cdll.LoadLibrary(
            "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
        )

        self.cg.CGEventCreate.argtypes = [ctypes.c_void_p]
        self.cg.CGEventCreate.restype = ctypes.c_void_p
        self.cg.CGEventGetLocation.argtypes = [ctypes.c_void_p]
        self.cg.CGEventGetLocation.restype = _CGPoint
        self.cg.CGEventCreateMouseEvent.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint32,
            _CGPoint,
            ctypes.c_uint32,
        ]
        self.cg.CGEventCreateMouseEvent.restype = ctypes.c_void_p
        self.cg.CGEventCreateScrollWheelEvent.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint32,
            ctypes.c_uint32,
            ctypes.c_int32,
        ]
        self.cg.CGEventCreateScrollWheelEvent.restype = ctypes.c_void_p
        self.cg.CGEventCreateKeyboardEvent.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint16,
            ctypes.c_bool,
        ]
        self.cg.CGEventCreateKeyboardEvent.restype = ctypes.c_void_p
        self.cg.CGEventSetFlags.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        self.cg.CGEventPost.argtypes = [ctypes.c_uint32, ctypes.c_void_p]
        self.cf.CFRelease.argtypes = [ctypes.c_void_p]

        self.lock = threading.Lock()
        self.dragging = False
        self.drag_mode = "orbit"
        self.origin = _CGPoint(0.0, 0.0)
        self.cur = _CGPoint(0.0, 0.0)
        self.release_timer: Optional[threading.Timer] = None

    def _cursor_pos(self) -> _CGPoint:
        ev = self.cg.CGEventCreate(None)
        pt = self.cg.CGEventGetLocation(ev)
        self.cf.CFRelease(ev)
        return pt

    def _post_mouse(self, ev_type: int, pt: _CGPoint, shift: bool = False) -> None:
        ev = self.cg.CGEventCreateMouseEvent(
            None, ev_type, pt, self.kCGMouseButtonCenter
        )
        if shift:
            self.cg.CGEventSetFlags(ev, self.kCGEventFlagMaskShift)
        self.cg.CGEventPost(self.kCGHIDEventTap, ev)
        self.cf.CFRelease(ev)

    def _post_scroll(self, delta_px: int) -> None:
        ev = self.cg.CGEventCreateScrollWheelEvent(
            None, self.kCGScrollEventUnitPixel, 1, int(delta_px)
        )
        self.cg.CGEventPost(self.kCGHIDEventTap, ev)
        self.cf.CFRelease(ev)

    def _post_frame_selected(self) -> None:
        for down in (True, False):
            ev = self.cg.CGEventCreateKeyboardEvent(
                None, self.kVK_ANSI_KeypadDecimal, down
            )
            self.cg.CGEventPost(self.kCGHIDEventTap, ev)
            self.cf.CFRelease(ev)

    def end_drag(self) -> None:
        with self.lock:
            if self.release_timer:
                self.release_timer.cancel()
                self.release_timer = None
            if self.dragging:
                self._post_mouse(
                    self.kCGEventOtherMouseUp,
                    self.cur,
                    shift=(self.drag_mode == "pan"),
                )
                self.dragging = False

    def handle(self, act: str, mode: str, dx: int, dy: int) -> None:
        if act == "frame":
            self.end_drag()
            self._post_frame_selected()
            return

        if act == "end":
            self.end_drag()
            return

        if mode == "zoom":
            # Touchpad Y-drag or dial detent -> smooth zoom scroll
            delta = (dx * 28) if act == "dial" else (-dy * 4)
            if delta != 0:
                self._post_scroll(delta)
            return

        if act == "dial":
            # Single-detent turntable orbit or horizontal pan
            step_x = dx * 22
            pt = self._cursor_pos()
            shift = mode == "pan"
            self._post_mouse(self.kCGEventOtherMouseDown, pt, shift=shift)
            time.sleep(0.004)
            self._post_mouse(
                self.kCGEventOtherMouseDragged,
                _CGPoint(pt.x + step_x, pt.y),
                shift=shift,
            )
            time.sleep(0.004)
            self._post_mouse(
                self.kCGEventOtherMouseUp,
                _CGPoint(pt.x + step_x, pt.y),
                shift=shift,
            )
            return

        if act == "drag":
            with self.lock:
                shift = mode == "pan"
                if not self.dragging or self.drag_mode != mode:
                    if self.dragging:
                        self._post_mouse(
                            self.kCGEventOtherMouseUp,
                            self.cur,
                            shift=(self.drag_mode == "pan"),
                        )
                    self.origin = self._cursor_pos()
                    self.cur = _CGPoint(self.origin.x, self.origin.y)
                    self.drag_mode = mode
                    self.dragging = True
                    self._post_mouse(
                        self.kCGEventOtherMouseDown, self.cur, shift=shift
                    )

                scale = 2.4
                self.cur = _CGPoint(self.cur.x + dx * scale, self.cur.y + dy * scale)
                self._post_mouse(
                    self.kCGEventOtherMouseDragged, self.cur, shift=shift
                )

                if self.release_timer:
                    self.release_timer.cancel()
                self.release_timer = threading.Timer(0.35, self.end_drag)
                self.release_timer.daemon = True
                self.release_timer.start()


def find_crowpanel_port(explicit_port: Optional[str] = None) -> Optional[str]:
    if explicit_port:
        return explicit_port
    for p in serial.tools.list_ports.comports():
        # Espressif native USB JTAG/Serial: VID 0x303A, PID 0x1001
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    candidates = sorted(glob.glob("/dev/cu.usbmodem*"))
    return candidates[0] if candidates else None


class PendingApproval:
    def __init__(self, req_id: str, platform: str):
        self.id = req_id
        self.platform = platform
        self.event = threading.Event()
        self.decision: str = "ask"  # fallback if timed out


class RotaryHubDaemon:
    def __init__(self, port: Optional[str] = None):
        self.explicit_port = port
        self.ser: Optional[serial.Serial] = None
        self.ser_lock = threading.Lock()
        self.running = True

        self.current_vol, self.current_muted = get_mac_volume()
        self.last_vol_write_at = 0.0
        self.last_media_payload: Dict[str, Any] = {}
        self.last_art_hash: str = ""
        self.last_art_bytes: Optional[bytes] = None
        self.last_art_rgb: list[int] = [0, 210, 240]
        self.last_agent_payload: Dict[str, Any] = {
            "t": "agent",
            "platform": "antigravity",
            "state": "idle",
            "title": "Connected",
            "detail": "Waiting for agent hooks",
        }

        self.approvals_lock = threading.Lock()
        self.pending_approvals: Dict[str, PendingApproval] = {}
        self.blender = MacBlenderController()

    def send_json(self, payload: Dict[str, Any]) -> bool:
        line = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        with self.ser_lock:
            if not self.ser or not self.ser.is_open:
                return False
            try:
                self.ser.write(line)
                self.ser.flush()
                return True
            except Exception as e:
                print(f"[serial] write error: {e}", file=sys.stderr)
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
                return False

    def send_art(self, art_bytes: Optional[bytes]) -> bool:
        with self.ser_lock:
            if not self.ser or not self.ser.is_open:
                return False
            try:
                if not art_bytes:
                    self.ser.write(b'{"t":"art_clear"}\n')
                    self.ser.flush()
                    return True
                # Send framed Base64 JSON chunks with explicit byte offsets (zero desync/tearing)
                for offset in range(0, len(art_bytes), ART_CHUNK_RAW):
                    chunk = art_bytes[offset : offset + ART_CHUNK_RAW]
                    b64 = base64.b64encode(chunk).decode("ascii")
                    line = (
                        json.dumps({"t": "ac", "o": offset, "d": b64}, separators=(",", ":"))
                        + "\n"
                    ).encode("utf-8")
                    self.ser.write(line)
                    self.ser.flush()
                    time.sleep(0.004)
                self.ser.write(b'{"t":"ae"}\n')
                self.ser.flush()
                return True
            except Exception as e:
                print(f"[art] serial send error: {e}", file=sys.stderr)
                return False

    def serial_loop(self) -> None:
        while self.running:
            port = find_crowpanel_port(self.explicit_port)
            if not port:
                time.sleep(1.5)
                continue

            try:
                print(f"[serial] connecting to {port} @ {BAUD}...")
                s = serial.Serial()
                s.port = port
                s.baudrate = BAUD
                s.timeout = 0.25
                s.dtr = False
                s.rts = False
                s.open()
                with self.ser_lock:
                    self.ser = s
                print(f"[serial] connected to {port}")
                time.sleep(0.3)
                # Push initial state immediately
                if self.last_media_payload:
                    self.send_json(self.last_media_payload)
                if self.last_art_bytes:
                    self.send_art(self.last_art_bytes)
                self.send_json(self.last_agent_payload)

                while self.running and s.is_open:
                    raw = s.readline()
                    if not raw:
                        continue
                    try:
                        msg = json.loads(raw.decode("utf-8", errors="ignore").strip())
                        self.handle_device_event(msg)
                    except json.JSONDecodeError:
                        pass
            except Exception as e:
                print(f"[serial] disconnected ({e}); retrying...", file=sys.stderr)
            finally:
                with self.ser_lock:
                    if self.ser:
                        try:
                            self.ser.close()
                        except Exception:
                            pass
                        self.ser = None
            time.sleep(1.5)

    def handle_device_event(self, msg: Dict[str, Any]) -> None:
        ev = msg.get("e")
        if ev == "hello":
            print(f"[device] hello: {msg}")
            if self.last_media_payload:
                self.send_json(self.last_media_payload)
            if self.last_art_bytes:
                self.send_art(self.last_art_bytes)
            self.send_json(self.last_agent_payload)
        elif ev == "art_ok":
            print("[device] artwork rendered OK")
        elif ev == "vol":
            delta = int(msg.get("delta", 0))
            if delta != 0:
                self.current_vol = max(0, min(100, self.current_vol + delta * VOL_STEP))
                self.current_muted = False
                self.last_vol_write_at = time.time()
                set_mac_volume(self.current_vol)
        elif ev == "media":
            cmd = str(msg.get("cmd", ""))
            print(f"[media] cmd={cmd}")
            run_media_cmd(cmd)
        elif ev == "focus":
            plat = str(msg.get("platform", "antigravity"))
            print(f"[agent] focus platform={plat}")
            focus_mac_app(plat)
        elif ev == "blender":
            self.blender.handle(
                act=str(msg.get("act", "drag")),
                mode=str(msg.get("mode", "orbit")),
                dx=int(msg.get("dx", 0)),
                dy=int(msg.get("dy", 0)),
            )
        elif ev == "approve":
            req_id = str(msg.get("id", ""))
            decision = str(msg.get("decision", "ask"))
            print(f"[agent] approval id={req_id} decision={decision}")
            with self.approvals_lock:
                pending = self.pending_approvals.get(req_id)
            if pending:
                if decision == "focus":
                    focus_mac_app(pending.platform)
                    pending.decision = "ask"
                elif decision in ("allow", "deny"):
                    pending.decision = decision
                else:
                    pending.decision = "ask"
                pending.event.set()

    def media_poll_loop(self) -> None:
        mc = shutil.which("media-control")
        if not mc:
            print(
                "[media] WARNING: 'media-control' not found. Install via:\n"
                "  brew tap ungive/media-control && brew install media-control",
                file=sys.stderr,
            )

        last_sent = None
        last_ping = 0.0

        while self.running:
            # Refresh Mac volume unless user just twisted the knob <0.8s ago
            if time.time() - self.last_vol_write_at > 0.8:
                self.current_vol, self.current_muted = get_mac_volume()

            title = ""
            artist = ""
            app = "MAC MEDIA"
            playing = False
            pos = 0

            art_b64 = ""
            if mc:
                try:
                    out = subprocess.check_output([mc, "get"], text=True, timeout=1.5).strip()
                    if out and out != "null":
                        data = json.loads(out)
                        title = sanitize_ascii(str(data.get("title") or ""), 64)
                        artist = sanitize_ascii(str(data.get("artist") or ""), 48)
                        app = sanitize_ascii(friendly_app_name(str(data.get("bundleIdentifier") or "")), 20)
                        playing = bool(data.get("playing", False))
                        art_b64 = str(data.get("artworkData") or "")
                        dur = float(data.get("duration") or 0)
                        elap = float(data.get("elapsedTime") or 0)
                        if dur > 0:
                            pos = max(0, min(100, int((elap / dur) * 100)))
                except Exception:
                    pass

            # Decode new artwork if changed (throttled to avoid spamming live video thumbnails)
            art_hash = hashlib.sha1(art_b64.encode("ascii")).hexdigest() if art_b64 else ""
            pending_art_send = False
            if art_hash != self.last_art_hash:
                self.last_art_hash = art_hash
                if art_b64:
                    art_bytes, accent_rgb = process_artwork_rgb565(art_b64)
                    self.last_art_bytes = art_bytes
                    self.last_art_rgb = accent_rgb
                    pending_art_send = bool(art_bytes)
                else:
                    self.last_art_bytes = None
                    self.last_art_rgb = [0, 210, 240]
                    pending_art_send = True

            payload = {
                "t": "media",
                "title": title or "Nothing Playing",
                "artist": artist or "macOS Now Playing",
                "app": app,
                "playing": playing,
                "vol": self.current_vol,
                "muted": self.current_muted,
                "pos": pos,
                "rgb": self.last_art_rgb,
            }
            self.last_media_payload = payload

            now = time.time()
            if payload != last_sent or (now - last_ping) > 5.0:
                if self.send_json(payload):
                    last_sent = dict(payload)
                    last_ping = now

            if pending_art_send:
                if self.last_art_bytes:
                    print(f"[art] streaming {ART_SIZE}x{ART_SIZE} artwork (accent={self.last_art_rgb})")
                    self.send_art(self.last_art_bytes)
                else:
                    self.send_art(None)

            time.sleep(0.8)

    def push_agent_event(self, platform: str, state: str, title: str, detail: str) -> None:
        payload = {
            "t": "agent",
            "platform": sanitize_ascii(platform or "agent", 20),
            "state": state or "idle",
            "title": sanitize_ascii(title or "", 44),
            "detail": sanitize_ascii(detail or "", 72),
        }
        self.last_agent_payload = payload
        self.send_json(payload)

    def request_approval(
        self, platform: str, tool: str, detail: str, timeout: float = 25.0
    ) -> str:
        req_id = uuid.uuid4().hex[:8]
        pending = PendingApproval(req_id, platform)
        with self.approvals_lock:
            self.pending_approvals[req_id] = pending

        sent = self.send_json(
            {
                "t": "approve",
                "id": req_id,
                "platform": sanitize_ascii(platform or "agent", 20),
                "tool": sanitize_ascii(tool or "Action", 32),
                "detail": sanitize_ascii(detail or "", 84),
            }
        )
        if not sent:
            with self.approvals_lock:
                self.pending_approvals.pop(req_id, None)
            return "ask"

        pending.event.wait(timeout=timeout)
        decision = pending.decision

        with self.approvals_lock:
            self.pending_approvals.pop(req_id, None)

        if not pending.event.is_set():
            # Timed out without user input -> clear knob overlay & fall back to IDE
            self.send_json({"t": "approve_clear", "id": req_id})

        return decision


def make_http_handler(daemon: RotaryHubDaemon):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: Any) -> None:
            pass  # keep console clean

        def _send_json(self, code: int, obj: Dict[str, Any]) -> None:
            body = json.dumps(obj).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            if self.path == "/health":
                self._send_json(
                    200,
                    {
                        "ok": True,
                        "connected": bool(daemon.ser and daemon.ser.is_open),
                    },
                )
            else:
                self._send_json(404, {"error": "not found"})

        def do_POST(self) -> None:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length) if length > 0 else b"{}"
            try:
                data = json.loads(raw.decode("utf-8"))
            except Exception:
                self._send_json(400, {"error": "invalid json"})
                return

            if self.path == "/api/agent/event":
                daemon.push_agent_event(
                    platform=str(data.get("platform", "agent")),
                    state=str(data.get("state", "idle")),
                    title=str(data.get("title", "")),
                    detail=str(data.get("detail", "")),
                )
                self._send_json(200, {"ok": True})
            elif self.path == "/api/agent/approve":
                timeout = float(data.get("timeout", 25.0))
                decision = daemon.request_approval(
                    platform=str(data.get("platform", "agent")),
                    tool=str(data.get("tool", "Action")),
                    detail=str(data.get("detail", "")),
                    timeout=timeout,
                )
                self._send_json(200, {"decision": decision})
            else:
                self._send_json(404, {"error": "not found"})

    return Handler


class ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main() -> None:
    parser = argparse.ArgumentParser(description="ESP Rotary Hub macOS Companion Daemon")
    parser.add_argument("--port", help="Explicit serial port (e.g. /dev/cu.usbmodem14101)")
    parser.add_argument("--http-port", type=int, default=PORT, help=f"HTTP bridge port (default {PORT})")
    args = parser.parse_args()

    daemon = RotaryHubDaemon(port=args.port)

    t_ser = threading.Thread(target=daemon.serial_loop, daemon=True)
    t_med = threading.Thread(target=daemon.media_poll_loop, daemon=True)
    t_ser.start()
    t_med.start()

    server = ThreadingHTTPServer((HOST, args.http_port), make_http_handler(daemon))
    print(f"[http] Agent bridge listening on http://{HOST}:{args.http_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[daemon] shutting down...")
        daemon.running = False
        server.shutdown()


if __name__ == "__main__":
    main()
