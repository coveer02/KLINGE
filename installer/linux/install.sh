#!/usr/bin/env bash
# Installs KLINGE for the current user (~/.vst3 and ~/.clap).
# Run with --system to install for all users (/usr/lib/vst3 and /usr/lib/clap, needs sudo).
set -euo pipefail
cd "$(dirname "$0")"

if [ "${1:-}" = "--system" ]; then
    VST3_DIR=/usr/lib/vst3
    CLAP_DIR=/usr/lib/clap
else
    VST3_DIR="$HOME/.vst3"
    CLAP_DIR="$HOME/.clap"
fi

mkdir -p "$VST3_DIR" "$CLAP_DIR"
rm -rf "$VST3_DIR/KLINGE.vst3"
cp -R KLINGE.vst3 "$VST3_DIR/"
cp KLINGE.clap "$CLAP_DIR/"
echo "KLINGE installed:"
echo "  VST3  $VST3_DIR/KLINGE.vst3"
echo "  CLAP  $CLAP_DIR/KLINGE.clap"
echo "Rescan plugins in your host."
