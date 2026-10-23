#!/usr/bin/env bash
# Install or update Spotify Soloist (Spotify Connect device "Buddy") in ~/buddy/bin. Runs ON the Pi:
# setup-pi.sh calls it once, buddy-soloist-update.timer weekly.
#
# Soloist builds stop working 90 days after their build date (exit code 10), and Spotify doesn't allow
# redistributing them, so the Pi always fetches the current build from Spotify. No pinned checksum:
# the file changes with every release.
set -euo pipefail
BIN="$HOME/buddy/bin/soloist"

case "$(uname -m)" in
    aarch64) arch=arm64 ;;
    x86_64)  arch=x86_64 ;;
    *) echo "no Soloist build for $(uname -m)" >&2; exit 1 ;;
esac

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
curl -fsSL --proto '=https' --proto-redir '=https' -o "$tmp/soloist.tgz" \
    "https://soloist-builds.spotifycdn.com/soloist_release_$arch.tar.gz"
tar xzf "$tmp/soloist.tgz" -C "$tmp" soloist

new="$("$tmp/soloist" --version)"
old="$("$BIN" --version 2>/dev/null || true)"
if [[ "$new" == "$old" ]]; then
    echo "  ✓ soloist $new (current)"
    exit 0
fi
mkdir -p "$(dirname "$BIN")"
install -m 755 "$tmp/soloist" "$BIN"
echo "  ✓ soloist ${old:-(new)} → $new"
if systemctl --user is-enabled --quiet buddy-soloist 2>/dev/null; then
    systemctl --user restart buddy-soloist
fi
