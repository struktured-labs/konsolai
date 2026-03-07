"""Abstract backend interface for GUI introspection and automation.

Both the AT-SPI (Linux) and UIA (Windows) backends implement this ABC.
The MCP server tools call these methods — they never touch platform APIs directly.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from .types import AppInfo, ClickResult, WidgetInfo, WidgetNode, WidgetState


class GuiBackend(ABC):
    """Platform-agnostic interface for GUI widget introspection and automation."""

    # ------------------------------------------------------------------
    # Discovery
    # ------------------------------------------------------------------

    @abstractmethod
    def list_applications(self) -> list[AppInfo]:
        """Return all accessible applications on the desktop.

        Each entry includes the app name, PID, and toolkit (e.g. "Qt").
        """

    @abstractmethod
    def get_widget_tree(self, app_name: str, max_depth: int = 5) -> WidgetNode:
        """Return the widget hierarchy of *app_name* up to *max_depth* levels.

        The root node is the application itself.  Children are nested
        recursively.  Use a shallow depth for overview, deeper for detail.
        """

    @abstractmethod
    def find_widget(
        self,
        app_name: str,
        *,
        role: str | None = None,
        name: str | None = None,
        auto_id: str | None = None,
    ) -> list[WidgetInfo]:
        """Search for widgets matching the given criteria.

        Parameters are ANDed together — all specified must match.
        *role* uses the normalized role vocabulary (see types.py).
        *name* matches the widget's accessible name (substring, case-insensitive).
        *auto_id* matches objectName (Qt) / AutomationId (UIA) exactly.

        Returns a list of matching widgets with their full paths.
        """

    # ------------------------------------------------------------------
    # Interaction
    # ------------------------------------------------------------------

    @abstractmethod
    def click_widget(self, widget_path: str) -> ClickResult:
        """Perform a click (activate/press) on the widget at *widget_path*.

        Uses the accessibility Action interface, not mouse coordinates.
        """

    @abstractmethod
    def type_text(self, widget_path: str, text: str) -> None:
        """Type *text* into the editable widget at *widget_path*.

        Replaces existing content.  The widget must support EditableText
        (AT-SPI) or ValuePattern (UIA).
        """

    @abstractmethod
    def read_text(self, widget_path: str) -> str:
        """Read the text content of the widget at *widget_path*.

        Works for labels, text fields, and any widget with a Text interface.
        """

    @abstractmethod
    def get_widget_state(self, widget_path: str) -> WidgetState:
        """Return the observable state of the widget at *widget_path*.

        Includes enabled, visible, focused, checked, expanded, selected.
        """

    @abstractmethod
    def get_widget_properties(self, widget_path: str) -> dict[str, str]:
        """Return all accessible properties/attributes of the widget.

        The exact keys depend on the toolkit and widget type.
        """

    @abstractmethod
    def select_tab(self, tab_widget_path: str, tab_name: str) -> None:
        """Switch to the tab named *tab_name* in the tab widget.

        *tab_widget_path* should point to a ``tab_list`` role widget.
        """

    @abstractmethod
    def expand_tree_node(self, node_path: str, expand: bool = True) -> None:
        """Expand or collapse the tree node at *node_path*.

        *node_path* should point to a ``tree_item`` role widget.
        """

    @abstractmethod
    def send_keys(self, keys: str) -> None:
        """Send keyboard input to the currently focused application.

        Key syntax:
        - Plain characters: ``"hello"``
        - Special keys in braces: ``"{Enter}"``, ``"{Tab}"``, ``"{Escape}"``
        - Modifiers: ``"{Ctrl+c}"``, ``"{Alt+F4}"``, ``"{Shift+Tab}"``
        """

    @abstractmethod
    def take_screenshot(self, widget_path: str | None = None) -> bytes:
        """Capture a PNG screenshot.

        If *widget_path* is given, capture only that widget's bounding rect.
        Otherwise capture the entire focused window.

        Returns raw PNG bytes.
        """
