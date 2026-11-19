#!/usr/bin/env python3
"""
Universal Coding-Agent Hook Bridge for ESP Rotary Hub.

Works with:
- Antigravity hooks (`hooks.json`: PreToolUse, PostToolUse, PreInvocation, Stop)
- Claude Code hooks (`~/.claude/settings.json`: PermissionRequest, PreToolUse, Notification, Stop)
- Any other IDE/CLI via `--platform <name> --event <state> --title ... --detail ...`

Reads the hook JSON payload from stdin, talks to the local Rotary Hub daemon
(http://127.0.0.1:48912), and prints the platform-appropriate JSON response to stdout.
Fails open (returns `ask` / `{}`) if the daemon or knob isn't running so it NEVER breaks your IDE!
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from typing import Any, Dict

DAEMON_URL = "http://127.0.0.1:48912"


def post_json(path: str, payload: Dict[str, Any], timeout: float = 2.0) -> Dict[str, Any] | None:
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"{DAEMON_URL}{path}",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except Exception:
        return None


def summarize_tool(tool_name: str, args: Any) -> str:
    if isinstance(args, dict):
        for key in ("CommandLine", "command"):
            if key in args and args[key]:
                return str(args[key])[:84]
        for key in ("Path", "file_path", "path"):
            if key in args and args[key]:
                val = str(args[key])
                # Show short tail (parent/file) so it fits cleanly on the round dial
                parts = [p for p in val.replace("\\", "/").split("/") if p]
                return "/".join(parts[-2:])[:72] if len(parts) >= 2 else val[:72]
        for key in ("query", "url", "Description", "Role"):
            if key in args and args[key]:
                return str(args[key])[:72]
        try:
            return json.dumps(args)[:72]
        except Exception:
            return ""
    return str(args or "")[:72]


def handle_antigravity(payload: Dict[str, Any], interactive_approve: bool, approve_timeout: float) -> Dict[str, Any]:
    event = payload.get("hookEventName", "")

    if event == "PreToolUse":
        tc = payload.get("toolCall", {})
        tool = tc.get("name", "tool")
        detail = summarize_tool(tool, tc.get("args", {}))

        if interactive_approve:
            post_json(
                "/api/agent/event",
                {"platform": "antigravity", "state": "waiting", "title": tool, "detail": detail},
            )
            res = post_json(
                "/api/agent/approve",
                {
                    "platform": "antigravity",
                    "tool": tool,
                    "detail": detail,
                    "timeout": approve_timeout,
                },
                timeout=approve_timeout + 3.0,
            )
            decision = (res or {}).get("decision", "ask")
            if decision == "allow":
                post_json(
                    "/api/agent/event",
                    {"platform": "antigravity", "state": "working", "title": f"Running {tool}", "detail": detail},
                )
                return {"decision": "allow", "reason": "Approved on ESP Rotary Knob"}
            if decision == "deny":
                post_json(
                    "/api/agent/event",
                    {"platform": "antigravity", "state": "idle", "title": f"Denied {tool}", "detail": detail},
                )
                return {"decision": "deny", "reason": "Denied on ESP Rotary Knob"}
            return {"decision": "ask"}

        post_json(
            "/api/agent/event",
            {"platform": "antigravity", "state": "working", "title": tool, "detail": detail},
        )
        return {"decision": "allow"}

    if event == "PostToolUse":
        tc = payload.get("toolCall", {})
        tool = tc.get("name", "tool")
        err = payload.get("error")
        if err:
            post_json(
                "/api/agent/event",
                {"platform": "antigravity", "state": "error", "title": f"{tool} failed", "detail": str(err)[:72]},
            )
        return {}

    if event == "SessionStart":
        post_json(
            "/api/agent/event",
            {"platform": "antigravity", "state": "idle", "title": "Session Active", "detail": "Listening for prompts"},
        )
        return {}

    if event == "PreInvocation":
        prompt = str(payload.get("lastUserInput") or "Thinking...")[:72]
        post_json(
            "/api/agent/event",
            {"platform": "antigravity", "state": "working", "title": "Thinking", "detail": prompt},
        )
        return {}

    if event == "Stop":
        err = payload.get("error")
        if err:
            post_json(
                "/api/agent/event",
                {"platform": "antigravity", "state": "error", "title": "Stopped (Error)", "detail": str(err)[:72]},
            )
        else:
            post_json(
                "/api/agent/event",
                {"platform": "antigravity", "state": "done", "title": "Turn Complete", "detail": "Press knob to focus IDE"},
            )
        return {"decision": "stop"}

    return {}


def handle_claude_code(payload: Dict[str, Any], event_override: str, interactive_approve: bool, approve_timeout: float) -> Dict[str, Any]:
    event = event_override or payload.get("hook_event_name", "")

    if event in ("PermissionRequest", "PreToolUse"):
        tool = payload.get("tool_name", "Tool")
        detail = summarize_tool(tool, payload.get("tool_input", {}))

        if interactive_approve and event == "PermissionRequest":
            post_json(
                "/api/agent/event",
                {"platform": "claude-code", "state": "waiting", "title": tool, "detail": detail},
            )
            res = post_json(
                "/api/agent/approve",
                {
                    "platform": "claude-code",
                    "tool": tool,
                    "detail": detail,
                    "timeout": approve_timeout,
                },
                timeout=approve_timeout + 3.0,
            )
            decision = (res or {}).get("decision", "ask")
            if decision in ("allow", "deny"):
                state = "working" if decision == "allow" else "idle"
                post_json(
                    "/api/agent/event",
                    {"platform": "claude-code", "state": state, "title": f"{decision.title()}: {tool}", "detail": detail},
                )
                return {
                    "hookSpecificOutput": {
                        "hookEventName": "PermissionRequest",
                        "decision": {"behavior": decision},
                    }
                }
            return {}

        post_json(
            "/api/agent/event",
            {"platform": "claude-code", "state": "working", "title": tool, "detail": detail},
        )
        return {}

    if event == "Notification":
        msg = str(payload.get("message") or payload.get("title") or "Needs attention")[:72]
        post_json(
            "/api/agent/event",
            {"platform": "claude-code", "state": "waiting", "title": "Notification", "detail": msg},
        )
        return {}

    if event == "Stop":
        post_json(
            "/api/agent/event",
            {"platform": "claude-code", "state": "done", "title": "Turn Complete", "detail": "Press knob to focus VS Code"},
        )
        return {}

    return {}


def main() -> None:
    parser = argparse.ArgumentParser(description="ESP Rotary Hub Coding Agent Hook Bridge")
    parser.add_argument("--platform", default="antigravity", choices=["antigravity", "claude-code", "generic"])
    parser.add_argument("--event", default="", help="Optional event name override")
    parser.add_argument("--approve", action="store_true", help="Wait for interactive approval on the knob")
    parser.add_argument("--timeout", type=float, default=25.0, help="Approval timeout in seconds")
    parser.add_argument("--title", default="", help="Title for generic events")
    parser.add_argument("--detail", default="", help="Detail for generic events")
    args = parser.parse_args()

    if args.platform == "generic":
        post_json(
            "/api/agent/event",
            {
                "platform": args.event or "agent",
                "state": args.event or "working",
                "title": args.title,
                "detail": args.detail,
            },
        )
        return

    raw = sys.stdin.read().strip()
    try:
        payload = json.loads(raw) if raw else {}
    except Exception:
        payload = {}

    if args.platform == "antigravity":
        out = handle_antigravity(payload, args.approve, args.timeout)
    else:
        out = handle_claude_code(payload, args.event, args.approve, args.timeout)

    sys.stdout.write(json.dumps(out) + "\n")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
