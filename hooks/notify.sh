#!/usr/bin/env bash
# Simple helper to push a status notification from ANY tool, script, or CI job:
#   ./hooks/notify.sh <platform> <working|done|error|idle> "<title>" "<detail>"
# Example:
#   ./hooks/notify.sh vscode done "Build Passed" "All 42 tests green"

PLATFORM="${1:-agent}"
STATE="${2:-done}"
TITLE="${3:-Update}"
DETAIL="${4:-}"

curl -sS -X POST http://127.0.0.1:48912/api/agent/event \
  -H "Content-Type: application/json" \
  -d "{\"platform\":\"${PLATFORM}\",\"state\":\"${STATE}\",\"title\":\"${TITLE}\",\"detail\":\"${DETAIL}\"}" \
  >/dev/null 2>&1 || true
