# Keyboard Shortcuts

## Claude-Specific Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+Alt+A` | Approve permission prompt |
| `Ctrl+Alt+D` | Deny permission prompt |
| `Ctrl+Alt+X` | Stop / interrupt Claude |
| `Ctrl+Alt+R` | Restart Claude session |

## Session Management

| Shortcut | Action |
|----------|--------|
| `Ctrl+Shift+T` | New tab |
| `Ctrl+Shift+P` | Quick session switcher |

## Notes

- Claude shortcuts use `Ctrl+Alt+{key}` to avoid conflicts with Konsole's `Ctrl+Shift` namespace
- `Ctrl+Alt+S` is reserved by upstream Konsole for "Rename Session"
- `Ctrl+Alt+{N,M,H}` are reserved by upstream for profile/split operations
- Shortcuts are registered via `KActionCollection::setDefaultShortcut` for KDE conflict detection
