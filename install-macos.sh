#!/bin/bash
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$DIR/mk8dx-quickscore.plugin"
DEST_DIR="$HOME/Library/Application Support/obs-studio/plugins"
DEST="$DEST_DIR/mk8dx-quickscore.plugin"

if pgrep -x "OBS" >/dev/null 2>&1; then
  echo "OBS Studio が起動中です。OBS を完全に終了してから再実行してください。"
  exit 1
fi

if [ ! -d "$SRC" ]; then
  echo "エラー: $SRC が見つかりません。"
  exit 1
fi

mkdir -p "$DEST_DIR"
rm -rf "$DEST"
cp -R "$SRC" "$DEST"
xattr -cr "$DEST" 2>/dev/null || true
codesign --force --deep --sign - "$DEST" 2>/dev/null || true

echo "インストール完了: $DEST"
echo "OBS Studio を起動し、メニューの「ドック」から「Lounge即時集計」を開いてください。"
