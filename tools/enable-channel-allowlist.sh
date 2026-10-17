#!/bin/bash
# Allowlist session-intercom as a Claude Code channel plugin via managed
# settings, so sessions can use plain `--channels` (no dev-channels warning).
#
#   bash enable-channel-allowlist.sh          # install
#   bash enable-channel-allowlist.sh --undo   # remove
#
# The file REPLACES Anthropic's default channel allowlist, so the official
# channel plugins are listed too.
set -euo pipefail
TARGET=/etc/claude-code/managed-settings.json

if [ "${1:-}" = "--undo" ]; then
    sudo rm -f "$TARGET"
    echo "Removed $TARGET"
    exit 0
fi

if [ -e "$TARGET" ]; then
    BACKUP="$TARGET.bak.$(date +%Y%m%d%H%M%S)"
    sudo cp "$TARGET" "$BACKUP"
    echo "Existing file backed up to $BACKUP — it will be replaced."
fi

TMPFILE="$(mktemp)"
trap 'rm -f "$TMPFILE"' EXIT
cat >"$TMPFILE" <<'EOF'
{
  "channelsEnabled": true,
  "allowedChannelPlugins": [
    { "marketplace": "struktured-labs", "plugin": "session-intercom" },
    { "marketplace": "claude-plugins-official", "plugin": "telegram" },
    { "marketplace": "claude-plugins-official", "plugin": "discord" },
    { "marketplace": "claude-plugins-official", "plugin": "imessage" },
    { "marketplace": "claude-plugins-official", "plugin": "fakechat" }
  ]
}
EOF
python3 -m json.tool "$TMPFILE" >/dev/null

sudo mkdir -p /etc/claude-code
sudo install -m 644 "$TMPFILE" "$TARGET"
echo "Installed $TARGET:"
cat "$TARGET"
