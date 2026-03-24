#!/usr/bin/env bash
# Wrapper: run deploy-ime-stage.ps1 from MSYS2/Git Bash (Windows).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS1="$ROOT/scripts/deploy-ime-stage.ps1"
exec powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS1" "$@"
