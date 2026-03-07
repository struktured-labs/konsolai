"""Shared types for the GUI MCP server.

Both AT-SPI (Linux) and UIA (Windows) backends produce and consume these types.
Role names are normalized to a common vocabulary so MCP tools work identically
on both platforms.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Normalized role mapping
# ---------------------------------------------------------------------------
# Each backend maps its native roles to these strings.
# Add entries as needed — these cover the Qt widgets used in Konsolai.

NORMALIZED_ROLES: dict[str, list[str]] = {
    # normalized      # AT-SPI variants                    # UIA variants
    "button":         ["push button", "PUSH_BUTTON",       "Button"],
    "checkbox":       ["check box", "CHECK_BOX",           "CheckBox"],
    "radio_button":   ["radio button", "RADIO_BUTTON",     "RadioButton"],
    "tab_list":       ["page tab list", "PAGE_TAB_LIST",   "Tab"],
    "tab":            ["page tab", "PAGE_TAB",             "TabItem"],
    "tree":           ["tree table", "TREE_TABLE", "tree", "TREE",  "Tree", "DataGrid"],
    "tree_item":      ["table cell", "TABLE_CELL", "tree item", "TREE_ITEM",  "TreeItem", "DataItem"],
    "menu":           ["menu", "MENU",                     "Menu"],
    "menu_bar":       ["menu bar", "MENU_BAR",             "MenuBar"],
    "menu_item":      ["menu item", "MENU_ITEM",           "MenuItem"],
    "text_field":     ["text", "TEXT", "entry", "ENTRY",   "Edit"],
    "label":          ["label", "LABEL",                   "Text"],
    "combo_box":      ["combo box", "COMBO_BOX",           "ComboBox"],
    "window":         ["frame", "FRAME", "window", "WINDOW", "Window"],
    "dialog":         ["dialog", "DIALOG",                 "Window"],  # UIA uses Window for dialogs too
    "panel":          ["panel", "PANEL", "filler", "FILLER", "Pane", "Group"],
    "scroll_bar":     ["scroll bar", "SCROLL_BAR",         "ScrollBar"],
    "separator":      ["separator", "SEPARATOR",           "Separator"],
    "progress_bar":   ["progress bar", "PROGRESS_BAR",     "ProgressBar"],
    "toolbar":        ["tool bar", "TOOL_BAR",             "ToolBar"],
    "status_bar":     ["status bar", "STATUS_BAR",         "StatusBar"],
    "slider":         ["slider", "SLIDER",                 "Slider"],
    "spin_button":    ["spin button", "SPIN_BUTTON",       "Spinner"],
    "list":           ["list", "LIST",                     "List"],
    "list_item":      ["list item", "LIST_ITEM",           "ListItem"],
    "application":    ["application", "APPLICATION"],
    "unknown":        [],
}

# Build reverse lookup: native_name → normalized_role
_REVERSE: dict[str, str] = {}
for _norm, _variants in NORMALIZED_ROLES.items():
    for _v in _variants:
        _REVERSE[_v.lower()] = _norm


def normalize_role(native_role: str) -> str:
    """Map a native AT-SPI or UIA role name to its normalized form."""
    return _REVERSE.get(native_role.lower(), native_role.lower().replace(" ", "_"))


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class AppInfo:
    """An accessible application visible on the desktop."""
    name: str
    pid: int
    toolkit: str  # "Qt", "GTK", etc.


@dataclass
class WidgetInfo:
    """Flat description of a single widget."""
    path: str           # "/app/Window/Panel/Button" — slash-separated accessible names
    name: str           # Accessible name (may be empty)
    role: str           # Normalized role string
    auto_id: str = ""   # objectName (Qt) → AutomationId (UIA) / accessible ID
    children_count: int = 0

    def to_dict(self) -> dict:
        return {
            "path": self.path,
            "name": self.name,
            "role": self.role,
            "auto_id": self.auto_id,
            "children_count": self.children_count,
        }


@dataclass
class WidgetNode:
    """Recursive tree node for widget_tree output."""
    info: WidgetInfo
    children: list[WidgetNode] = field(default_factory=list)

    def to_dict(self) -> dict:
        d = self.info.to_dict()
        if self.children:
            d["children"] = [c.to_dict() for c in self.children]
        return d


@dataclass
class WidgetState:
    """Observable state of a widget."""
    enabled: bool = True
    visible: bool = True
    focused: bool = False
    checked: bool | None = None   # None if not a checkable widget
    expanded: bool | None = None  # None if not expandable
    selected: bool = False
    editable: bool = False

    def to_dict(self) -> dict:
        d: dict = {
            "enabled": self.enabled,
            "visible": self.visible,
            "focused": self.focused,
            "selected": self.selected,
            "editable": self.editable,
        }
        if self.checked is not None:
            d["checked"] = self.checked
        if self.expanded is not None:
            d["expanded"] = self.expanded
        return d


@dataclass
class ClickResult:
    """Outcome of a click action."""
    success: bool
    widget_name: str
    message: str = ""
