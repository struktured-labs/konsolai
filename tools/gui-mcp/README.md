# Konsolai GUI MCP Server

MCP server for GUI introspection and automation of Konsolai (Qt6).
Uses accessibility APIs — AT-SPI on Linux, UI Automation on Windows — to walk the widget tree, click buttons, read text, verify state, and more.

Works with Claude Code, Claude Desktop, Cursor, or any MCP client.

## Quick Start (Linux)

```bash
# System dependencies
sudo apt install at-spi2-core gir1.2-atspi-2.0 python3-gi

# Install
cd tools/gui-mcp
uv sync --extra linux

# Launch Konsolai with accessibility enabled
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 konsolai &

# Run the MCP server
uv run konsolai-gui-mcp

# Or debug with the MCP inspector
mcp dev src/konsolai_gui_mcp/server.py
```

## Quick Start (Windows)

```powershell
cd tools\gui-mcp
uv sync --extra windows

# Launch Konsolai (Qt exposes UIA automatically on Windows)
konsolai.exe

# Run the MCP server
uv run konsolai-gui-mcp
```

## Claude Code Configuration

Add to your project's `.claude/settings.json`:

```json
{
  "mcpServers": {
    "konsolai-gui": {
      "command": "uv",
      "args": ["run", "--directory", "tools/gui-mcp", "konsolai-gui-mcp"],
      "env": {
        "QT_LINUX_ACCESSIBILITY_ALWAYS_ON": "1"
      }
    }
  }
}
```

## MCP Tools

| Tool | Description | Example |
|------|-------------|---------|
| `list_apps` | List all accessible applications | `list_apps()` |
| `widget_tree` | Get widget hierarchy | `widget_tree("konsolai", max_depth=3)` |
| `find_widget` | Search by role/name/id | `find_widget("konsolai", role="button", name="New")` |
| `click` | Click a widget | `click("konsolai/MainWindow/New Session")` |
| `type_text` | Type into a text field | `type_text("konsolai/MainWindow/filter", "search term")` |
| `read_text` | Read widget text | `read_text("konsolai/MainWindow/StatusLabel")` |
| `widget_state` | Get enabled/visible/checked | `widget_state("konsolai/MainWindow/YoloMode")` |
| `select_tab` | Switch tabs | `select_tab("konsolai/MainWindow/tabs", "Session 2")` |
| `expand_node` | Expand/collapse tree node | `expand_node("konsolai/MainWindow/tree/Active", True)` |
| `send_keys` | Send keyboard input | `send_keys("{Ctrl+Shift+t}")` |
| `screenshot` | Capture PNG screenshot | `screenshot("konsolai/MainWindow")` |

### Widget Paths

Widgets are addressed by slash-separated accessible names from the app root:
```
konsolai/MainWindow/SessionManagerPanel/SessionTree/Active/my-session
```

Use `widget_tree` or `find_widget` to discover paths. The `auto_id` field corresponds to `QWidget::objectName()` — set this in your Qt code for reliable targeting.

### Normalized Roles

Both backends use normalized role names:

| Role | Qt Widget | Description |
|------|-----------|-------------|
| `button` | QPushButton | Clickable button |
| `checkbox` | QCheckBox | Toggle control |
| `tab_list` | QTabWidget | Tab container |
| `tab` | (tab item) | Individual tab |
| `tree` | QTreeWidget | Tree/table view |
| `tree_item` | QTreeWidgetItem | Tree node |
| `menu` | QMenu | Menu container |
| `menu_item` | QAction | Menu entry |
| `text_field` | QLineEdit/QTextEdit | Text input |
| `label` | QLabel | Static text |
| `combo_box` | QComboBox | Dropdown |
| `panel` | QWidget/QFrame | Container |

---

## Windows Implementation Guide

The Linux backend (`atspi_backend.py`) is fully implemented. The Windows backend (`uia_backend.py`) is a stub with detailed docstrings showing exactly how to implement each method using `pywinauto`.

### Step 1: Environment

```powershell
pip install pywinauto Pillow
```

No special Qt configuration is needed — Qt6 on Windows exposes UIA automatically.

### Step 2: Discover Your Widget Tree

```python
from pywinauto.application import Application

app = Application(backend="uia").connect(title_re=".*konsolai.*")
dlg = app.top_window()
dlg.print_control_identifiers()
```

This prints every widget with its `control_type`, `name`, `auto_id` (objectName), and the attribute names you can use in code.

### Step 3: Implement Each Method

Open `uia_backend.py`. Each method has a docstring containing the exact pywinauto code to implement it. The pattern is:

1. **`_resolve_path()`** — Walk the widget tree following the slash-separated path. Cache the `Application` connection.
2. **`list_applications()`** — Use `Desktop(backend="uia").windows()`
3. **`get_widget_tree()`** — Recursively traverse `ctrl.children()`
4. **`find_widget()`** — Use `dlg.descendants()` with criteria
5. **`click_widget()`** — `ctrl.invoke()` for buttons, `ctrl.click_input()` for others
6. **`type_text()`** — `ctrl.set_edit_text(text)`
7. **`read_text()`** — `ctrl.get_value()` or `ctrl.window_text()`
8. **`get_widget_state()`** — Read `element_info.enabled/visible` + patterns
9. **`select_tab()`** — `tab_ctrl.select(tab_name)`
10. **`expand_tree_node()`** — `node.expand()` / `node.collapse()`
11. **`send_keys()`** — `pywinauto.keyboard.send_keys()` with modifier translation
12. **`take_screenshot()`** — `PIL.ImageGrab.grab(bbox=rect)`

### Step 4: Key Mapping

Our key syntax → pywinauto translation:

| Our Syntax | pywinauto |
|-----------|-----------|
| `{Enter}` | `{ENTER}` |
| `{Tab}` | `{TAB}` |
| `{Escape}` | `{ESC}` |
| `{Ctrl+c}` | `^c` |
| `{Alt+F4}` | `%{F4}` |
| `{Shift+Tab}` | `+{TAB}` |

### Step 5: Test

```bash
mcp dev src/konsolai_gui_mcp/server.py
```

This launches the MCP inspector where you can call tools interactively and see results.

### Step 6: Tips

- **Always use `backend="uia"`**, not `"win32"`. The UIA backend sees far more Qt widgets.
- **`objectName` → `AutomationId`** is the most reliable locator. Set `setObjectName()` on widgets you want to automate.
- **Use `Accessibility Insights for Windows`** (free Microsoft tool) to inspect the UIA tree visually.
- **Cache connections** — `Application(backend="uia").connect()` is slow. Store the app/window reference and reuse it.
- **Qt Quick/QML apps** require explicit `Accessible.role` and `Accessible.name` in QML. QWidget apps work automatically.

## Architecture

```
┌─────────────────────┐
│   Claude / MCP      │
│   Client            │
└──────────┬──────────┘
           │ stdio
┌──────────▼──────────┐
│   server.py         │  MCP tools: list_apps, find_widget, click, ...
│   (FastMCP)         │
└──────────┬──────────┘
           │ delegates to
┌──────────▼──────────┐
│   GuiBackend (ABC)  │  Abstract interface (backend.py)
├─────────────────────┤
│ atspi_backend.py    │  Linux: gi.repository.Atspi (AT-SPI2 over D-Bus)
│ uia_backend.py      │  Windows: pywinauto (UI Automation over COM)
└─────────────────────┘
           │
    Qt6 Accessibility
    (built into QPA)
```
