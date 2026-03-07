"""Konsolai GUI MCP Server.

Exposes GUI introspection and automation tools via the Model Context Protocol.
Automatically selects the AT-SPI backend (Linux) or UIA backend (Windows).

Usage:
    # Run as MCP server (stdio transport):
    uv run konsolai-gui-mcp

    # Debug with MCP inspector:
    mcp dev src/konsolai_gui_mcp/server.py
"""

from __future__ import annotations

import base64
import json
import platform
from typing import Annotated

from mcp.server.fastmcp import FastMCP

from .backend import GuiBackend

mcp = FastMCP(
    "konsolai-gui",
    instructions="GUI introspection and automation for Konsolai (Qt6/KDE)",
)

_backend: GuiBackend | None = None


def _get_backend() -> GuiBackend:
    global _backend
    if _backend is not None:
        return _backend

    system = platform.system()
    if system == "Linux":
        from .atspi_backend import AtspiBackend
        _backend = AtspiBackend()
    elif system == "Windows":
        from .uia_backend import UiaBackend
        _backend = UiaBackend()
    else:
        raise RuntimeError(f"Unsupported platform: {system}. Supported: Linux (AT-SPI), Windows (UIA).")

    return _backend


# ---------------------------------------------------------------------------
# MCP Tools
# ---------------------------------------------------------------------------

@mcp.tool()
def list_apps() -> str:
    """List all accessible applications on the desktop.

    Returns application names, PIDs, and toolkits.
    Use the app name in other tools to target a specific application.
    """
    backend = _get_backend()
    apps = backend.list_applications()
    return json.dumps([{"name": a.name, "pid": a.pid, "toolkit": a.toolkit} for a in apps], indent=2)


@mcp.tool()
def widget_tree(
    app_name: Annotated[str, "Name of the application (e.g. 'konsolai')"],
    max_depth: Annotated[int, "Maximum depth to traverse (default 3)"] = 3,
) -> str:
    """Get the widget hierarchy of an application.

    Returns a nested tree of widgets with their roles, names, and paths.
    Use shallow depth (2-3) for overview, deeper (5+) for detail.
    Widget paths from this output can be used in other tools.
    """
    backend = _get_backend()
    tree = backend.get_widget_tree(app_name, max_depth=max_depth)
    return json.dumps(tree.to_dict(), indent=2)


@mcp.tool()
def find_widget(
    app_name: Annotated[str, "Name of the application"],
    role: Annotated[str | None, "Widget role to match (e.g. 'button', 'tab', 'tree_item')"] = None,
    name: Annotated[str | None, "Widget name substring to match (case-insensitive)"] = None,
    auto_id: Annotated[str | None, "Exact objectName / AutomationId to match"] = None,
) -> str:
    """Search for widgets matching the given criteria.

    All specified criteria are ANDed together. Returns matching widgets
    with their full paths (usable in click, read_text, etc.).

    Common roles: button, checkbox, tab, tab_list, tree, tree_item,
    menu_item, text_field, label, combo_box, window, panel
    """
    backend = _get_backend()
    results = backend.find_widget(app_name, role=role, name=name, auto_id=auto_id)
    return json.dumps([w.to_dict() for w in results], indent=2)


@mcp.tool()
def click(
    widget_path: Annotated[str, "Slash-separated path to the widget (from find_widget or widget_tree)"],
) -> str:
    """Click (activate) a widget.

    Uses the accessibility Action interface — no mouse coordinates needed.
    Works for buttons, menu items, checkboxes, tree nodes, etc.
    """
    backend = _get_backend()
    result = backend.click_widget(widget_path)
    return json.dumps({"success": result.success, "widget": result.widget_name, "message": result.message})


@mcp.tool()
def type_text(
    widget_path: Annotated[str, "Path to an editable text widget"],
    text: Annotated[str, "Text to type (replaces existing content)"],
) -> str:
    """Type text into an editable widget (QLineEdit, QTextEdit, etc.).

    Replaces existing content with the new text.
    """
    backend = _get_backend()
    try:
        backend.type_text(widget_path, text)
        return json.dumps({"success": True, "text": text})
    except Exception as e:
        return json.dumps({"success": False, "error": str(e)})


@mcp.tool()
def read_text(
    widget_path: Annotated[str, "Path to the widget to read"],
) -> str:
    """Read the text content of a widget.

    Works for text fields, labels, and any widget with readable text.
    """
    backend = _get_backend()
    text = backend.read_text(widget_path)
    return json.dumps({"text": text})


@mcp.tool()
def widget_state(
    widget_path: Annotated[str, "Path to the widget to inspect"],
) -> str:
    """Get the observable state of a widget.

    Returns: enabled, visible, focused, checked (for checkboxes),
    expanded (for tree nodes), selected, editable.
    """
    backend = _get_backend()
    state = backend.get_widget_state(widget_path)
    return json.dumps(state.to_dict())


@mcp.tool()
def widget_properties(
    widget_path: Annotated[str, "Path to the widget to inspect"],
) -> str:
    """Get all accessible properties of a widget.

    Returns role, name, automation ID, bounds, states, and toolkit-specific attributes.
    Useful for debugging widget identity and state.
    """
    backend = _get_backend()
    props = backend.get_widget_properties(widget_path)
    return json.dumps(props, indent=2)


@mcp.tool()
def select_tab(
    tab_path: Annotated[str, "Path to the tab list widget (role: tab_list)"],
    tab_name: Annotated[str, "Name of the tab to select"],
) -> str:
    """Switch to a tab by name.

    The tab_path should point to the tab container (role: tab_list),
    not an individual tab.
    """
    backend = _get_backend()
    try:
        backend.select_tab(tab_path, tab_name)
        return json.dumps({"success": True, "tab": tab_name})
    except Exception as e:
        return json.dumps({"success": False, "error": str(e)})


@mcp.tool()
def expand_node(
    node_path: Annotated[str, "Path to the tree node"],
    expand: Annotated[bool, "True to expand, False to collapse"] = True,
) -> str:
    """Expand or collapse a tree node.

    Works on tree_item role widgets in QTreeWidget/QTreeView.
    """
    backend = _get_backend()
    try:
        backend.expand_tree_node(node_path, expand=expand)
        return json.dumps({"success": True, "expanded": expand})
    except Exception as e:
        return json.dumps({"success": False, "error": str(e)})


@mcp.tool()
def send_keys(
    keys: Annotated[str, "Keys to send. Plain text or special keys in braces: {Enter}, {Tab}, {Ctrl+c}, {Alt+F4}"],
) -> str:
    """Send keyboard input to the currently focused application.

    Syntax:
    - Plain characters: "hello"
    - Special keys: {Enter}, {Tab}, {Escape}, {Backspace}
    - Modifiers: {Ctrl+c}, {Alt+F4}, {Shift+Tab}
    """
    backend = _get_backend()
    try:
        backend.send_keys(keys)
        return json.dumps({"success": True, "keys": keys})
    except Exception as e:
        return json.dumps({"success": False, "error": str(e)})


@mcp.tool()
def screenshot(
    widget_path: Annotated[str | None, "Path to widget to capture (omit for full window)"] = None,
) -> str:
    """Capture a PNG screenshot of a widget or the full window.

    Returns base64-encoded PNG data.
    """
    backend = _get_backend()
    try:
        png_bytes = backend.take_screenshot(widget_path)
        return json.dumps({
            "format": "png",
            "size_bytes": len(png_bytes),
            "data_base64": base64.b64encode(png_bytes).decode("ascii"),
        })
    except Exception as e:
        return json.dumps({"success": False, "error": str(e)})


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    """Run the MCP server."""
    mcp.run()


if __name__ == "__main__":
    main()
