#!/usr/bin/env bash
# Clone fcitx5-chinese-addons next to fcitx5-windows (optional helper).
# 完整构建流程见 docs/PINYIN_WINDOWS.md；libime 请自行克隆到 ../libime。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PARENT="$(cd "$ROOT/.." && pwd)"
REPO="${CHINESE_ADDONS_GIT:-https://github.com/fcitx/fcitx5-chinese-addons.git}"
TAG="${CHINESE_ADDONS_TAG:-5.1.12}"
DEST="${CHINESE_ADDONS_DIR:-$PARENT/fcitx5-chinese-addons}"

if [[ -d "$DEST/.git" ]]; then
  echo "Already exists: $DEST"
  exit 0
fi

if [[ -e "$DEST" ]]; then
  echo "Path exists and is not a git repo: $DEST" >&2
  exit 1
fi

git clone --depth 1 --branch "$TAG" "$REPO" "$DEST"
echo "Cloned chinese-addons to: $DEST"
echo "Next: follow docs/PINYIN_WINDOWS.md (stage prefix + libime + chinese-addons build)."
