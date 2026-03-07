"""Linux AT-SPI2 backend using gi.repository.Atspi (GObject Introspection).

Prerequisites:
    sudo apt install at-spi2-core gir1.2-atspi-2.0 python3-gi

    Launch the target Qt app with:
        QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 konsolai
"""

from __future__ import annotations

import subprocess
from typing import TYPE_CHECKING

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi  # noqa: E402

from .backend import GuiBackend
from .types import (
    AppInfo,
    ClickResult,
    WidgetInfo,
    WidgetNode,
    WidgetState,
    normalize_role,
)

if TYPE_CHECKING:
    pass


class AtspiBackend(GuiBackend):
    """AT-SPI2 implementation of the GUI backend."""

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _role_name(accessible: Atspi.Accessible) -> str:
        try:
            return Atspi.Accessible.get_role_name(accessible)
        except Exception:
            return "unknown"

    @staticmethod
    def _name(accessible: Atspi.Accessible) -> str:
        try:
            return accessible.get_name() or ""
        except Exception:
            return ""

    @staticmethod
    def _auto_id(accessible: Atspi.Accessible) -> str:
        """Extract objectName / automation ID from AT-SPI attributes."""
        try:
            attrs = accessible.get_attributes()
            if attrs:
                # Qt sets "objectName" in the attribute dict
                return attrs.get("objectName", "")
        except Exception:
            pass
        return ""

    def _build_path(self, accessible: Atspi.Accessible, prefix: str = "") -> str:
        name = self._name(accessible) or self._role_name(accessible)
        # Sanitize slashes in names
        name = name.replace("/", "|")
        return f"{prefix}/{name}" if prefix else name

    def _find_app(self, app_name: str) -> Atspi.Accessible | None:
        """Find an application by name (case-insensitive substring match)."""
        desktop = Atspi.get_desktop(0)
        app_name_lower = app_name.lower()
        for i in range(desktop.get_child_count()):
            app = desktop.get_child_at_index(i)
            if app and app_name_lower in (self._name(app) or "").lower():
                return app
        return None

    def _resolve_path(self, widget_path: str) -> Atspi.Accessible | None:
        """Walk the accessibility tree following a slash-separated path.

        Path format: "app_name/child_name/grandchild_name/..."
        At each level, matches by accessible name (case-insensitive).
        Falls back to role name if the accessible name is empty.
        """
        parts = [p for p in widget_path.split("/") if p]
        if not parts:
            return None

        # Find the application
        current = self._find_app(parts[0])
        if not current:
            return None

        for part in parts[1:]:
            part_lower = part.lower()
            found = False
            for i in range(current.get_child_count()):
                child = current.get_child_at_index(i)
                if not child:
                    continue
                child_label = (self._name(child) or self._role_name(child)).replace("/", "|")
                if child_label.lower() == part_lower:
                    current = child
                    found = True
                    break
            if not found:
                return None
        return current

    def _to_widget_info(self, accessible: Atspi.Accessible, path: str) -> WidgetInfo:
        return WidgetInfo(
            path=path,
            name=self._name(accessible),
            role=normalize_role(self._role_name(accessible)),
            auto_id=self._auto_id(accessible),
            children_count=accessible.get_child_count(),
        )

    def _build_tree(self, accessible: Atspi.Accessible, path: str, depth: int, max_depth: int) -> WidgetNode:
        info = self._to_widget_info(accessible, path)
        children: list[WidgetNode] = []
        if depth < max_depth:
            for i in range(accessible.get_child_count()):
                child = accessible.get_child_at_index(i)
                if child:
                    child_path = self._build_path(child, path)
                    children.append(self._build_tree(child, child_path, depth + 1, max_depth))
        return WidgetNode(info=info, children=children)

    def _search(
        self,
        accessible: Atspi.Accessible,
        path: str,
        *,
        role: str | None,
        name: str | None,
        auto_id: str | None,
        results: list[WidgetInfo],
        max_results: int = 50,
    ) -> None:
        if len(results) >= max_results:
            return

        matches = True
        if role and normalize_role(self._role_name(accessible)) != role:
            matches = False
        if name and name.lower() not in (self._name(accessible) or "").lower():
            matches = False
        if auto_id and self._auto_id(accessible) != auto_id:
            matches = False

        if matches and (role or name or auto_id):
            results.append(self._to_widget_info(accessible, path))

        for i in range(accessible.get_child_count()):
            child = accessible.get_child_at_index(i)
            if child:
                child_path = self._build_path(child, path)
                self._search(child, child_path, role=role, name=name, auto_id=auto_id, results=results, max_results=max_results)

    # ------------------------------------------------------------------
    # GuiBackend implementation
    # ------------------------------------------------------------------

    def list_applications(self) -> list[AppInfo]:
        desktop = Atspi.get_desktop(0)
        apps: list[AppInfo] = []
        for i in range(desktop.get_child_count()):
            app = desktop.get_child_at_index(i)
            if not app:
                continue
            try:
                toolkit = app.get_toolkit_name() or "unknown"
            except Exception:
                toolkit = "unknown"
            try:
                pid = app.get_process_id()
            except Exception:
                pid = -1
            apps.append(AppInfo(
                name=self._name(app) or f"app-{i}",
                pid=pid,
                toolkit=toolkit,
            ))
        return apps

    def get_widget_tree(self, app_name: str, max_depth: int = 5) -> WidgetNode:
        app = self._find_app(app_name)
        if not app:
            raise ValueError(f"Application '{app_name}' not found. Available: {[a.name for a in self.list_applications()]}")
        path = self._name(app) or "app"
        return self._build_tree(app, path, 0, max_depth)

    def find_widget(
        self,
        app_name: str,
        *,
        role: str | None = None,
        name: str | None = None,
        auto_id: str | None = None,
    ) -> list[WidgetInfo]:
        app = self._find_app(app_name)
        if not app:
            raise ValueError(f"Application '{app_name}' not found")
        results: list[WidgetInfo] = []
        path = self._name(app) or "app"
        self._search(app, path, role=role, name=name, auto_id=auto_id, results=results)
        return results

    def click_widget(self, widget_path: str) -> ClickResult:
        accessible = self._resolve_path(widget_path)
        if not accessible:
            return ClickResult(success=False, widget_name="", message=f"Widget not found: {widget_path}")

        action_iface = accessible.get_action_iface()
        if not action_iface:
            return ClickResult(success=False, widget_name=self._name(accessible), message="Widget has no Action interface")

        # Try common action names
        n_actions = action_iface.get_n_actions()
        for i in range(n_actions):
            action_name = action_iface.get_action_name(i)
            if action_name in ("click", "activate", "press", "toggle", "SetFocus"):
                action_iface.do_action(i)
                return ClickResult(success=True, widget_name=self._name(accessible))

        # Fall back to first action
        if n_actions > 0:
            action_iface.do_action(0)
            return ClickResult(success=True, widget_name=self._name(accessible))

        return ClickResult(success=False, widget_name=self._name(accessible), message="No suitable action found")

    def type_text(self, widget_path: str, text: str) -> None:
        accessible = self._resolve_path(widget_path)
        if not accessible:
            raise ValueError(f"Widget not found: {widget_path}")

        edit_iface = accessible.get_editable_text_iface()
        if not edit_iface:
            raise ValueError(f"Widget at '{widget_path}' is not editable")

        # Clear existing text
        text_iface = accessible.get_text_iface()
        if text_iface:
            length = text_iface.get_character_count()
            if length > 0:
                edit_iface.delete_text(0, length)

        edit_iface.insert_text(0, text, len(text))

    def read_text(self, widget_path: str) -> str:
        accessible = self._resolve_path(widget_path)
        if not accessible:
            raise ValueError(f"Widget not found: {widget_path}")

        text_iface = accessible.get_text_iface()
        if text_iface:
            length = text_iface.get_character_count()
            return text_iface.get_text(0, length)

        # Fall back to the accessible name (works for labels)
        return self._name(accessible)

    def get_widget_state(self, widget_path: str) -> WidgetState:
        accessible = self._resolve_path(widget_path)
        if not accessible:
            raise ValueError(f"Widget not found: {widget_path}")

        state_set = accessible.get_state_set()
        role = self._role_name(accessible)

        checked: bool | None = None
        if normalize_role(role) in ("checkbox", "radio_button"):
            checked = state_set.contains(Atspi.StateType.CHECKED)

        expanded: bool | None = None
        if state_set.contains(Atspi.StateType.EXPANDABLE):
            expanded = state_set.contains(Atspi.StateType.EXPANDED)

        return WidgetState(
            enabled=state_set.contains(Atspi.StateType.ENABLED),
            visible=state_set.contains(Atspi.StateType.SHOWING),
            focused=state_set.contains(Atspi.StateType.FOCUSED),
            checked=checked,
            expanded=expanded,
            selected=state_set.contains(Atspi.StateType.SELECTED),
            editable=state_set.contains(Atspi.StateType.EDITABLE),
        )

    def get_widget_properties(self, widget_path: str) -> dict[str, str]:
        accessible = self._resolve_path(widget_path)
        if not accessible:
            raise ValueError(f"Widget not found: {widget_path}")

        props: dict[str, str] = {}
        props["name"] = self._name(accessible)
        props["role"] = self._role_name(accessible)
        props["normalized_role"] = normalize_role(self._role_name(accessible))

        try:
            attrs = accessible.get_attributes()
            if attrs:
                props.update(attrs)
        except Exception:
            pass

        try:
            state_set = accessible.get_state_set()
            states = state_set.get_states()
            props["states"] = ", ".join(Atspi.StateType.get_value_name(s) for s in states)
        except Exception:
            pass

        try:
            comp = accessible.get_component_iface()
            if comp:
                rect = comp.get_extents(Atspi.CoordType.SCREEN)
                props["bounds"] = f"{rect.x},{rect.y},{rect.width},{rect.height}"
        except Exception:
            pass

        return props

    def select_tab(self, tab_widget_path: str, tab_name: str) -> None:
        accessible = self._resolve_path(tab_widget_path)
        if not accessible:
            raise ValueError(f"Tab widget not found: {tab_widget_path}")

        selection = accessible.get_selection_iface()
        if selection:
            # Find the tab by name and select it
            for i in range(accessible.get_child_count()):
                child = accessible.get_child_at_index(i)
                if child and tab_name.lower() in (self._name(child) or "").lower():
                    selection.select_child(i)
                    return
            raise ValueError(f"Tab '{tab_name}' not found in {tab_widget_path}")

        # Fallback: try clicking the tab directly
        for i in range(accessible.get_child_count()):
            child = accessible.get_child_at_index(i)
            if child and tab_name.lower() in (self._name(child) or "").lower():
                action = child.get_action_iface()
                if action and action.get_n_actions() > 0:
                    action.do_action(0)
                    return
        raise ValueError(f"Tab '{tab_name}' not found or not activatable")

    def expand_tree_node(self, node_path: str, expand: bool = True) -> None:
        accessible = self._resolve_path(node_path)
        if not accessible:
            raise ValueError(f"Tree node not found: {node_path}")

        action_iface = accessible.get_action_iface()
        if not action_iface:
            raise ValueError(f"Tree node at '{node_path}' has no Action interface")

        target_action = "expand or contract" if expand else "expand or contract"
        n_actions = action_iface.get_n_actions()
        for i in range(n_actions):
            action_name = action_iface.get_action_name(i).lower()
            if "expand" in action_name or "collapse" in action_name:
                state_set = accessible.get_state_set()
                is_expanded = state_set.contains(Atspi.StateType.EXPANDED)
                if (expand and not is_expanded) or (not expand and is_expanded):
                    action_iface.do_action(i)
                return

        raise ValueError(f"No expand/collapse action found on '{node_path}'")

    def send_keys(self, keys: str) -> None:
        """Send keys using xdotool as AT-SPI doesn't have a direct key synthesis API.

        Key syntax:
        - Plain text: "hello"
        - Special: "{Enter}", "{Tab}", "{Escape}"
        - Modifiers: "{Ctrl+c}", "{Alt+F4}"
        """
        # Parse brace-enclosed special keys
        import re

        parts = re.split(r"(\{[^}]+\})", keys)
        for part in parts:
            if not part:
                continue
            if part.startswith("{") and part.endswith("}"):
                key_spec = part[1:-1]
                # Map common names to xdotool names
                key_map = {
                    "Enter": "Return", "Tab": "Tab", "Escape": "Escape",
                    "Space": "space", "Backspace": "BackSpace", "Delete": "Delete",
                    "Up": "Up", "Down": "Down", "Left": "Left", "Right": "Right",
                    "Home": "Home", "End": "End", "PageUp": "Prior", "PageDown": "Next",
                }
                if "+" in key_spec:
                    # Modifier combo: Ctrl+c → xdotool key ctrl+c
                    xdo_key = key_spec.replace("Ctrl", "ctrl").replace("Alt", "alt").replace("Shift", "shift")
                    subprocess.run(["xdotool", "key", xdo_key], check=True)
                else:
                    xdo_key = key_map.get(key_spec, key_spec)
                    subprocess.run(["xdotool", "key", xdo_key], check=True)
            else:
                # Plain text
                subprocess.run(["xdotool", "type", "--clearmodifiers", part], check=True)

    def take_screenshot(self, widget_path: str | None = None) -> bytes:
        """Capture a screenshot using the platform screenshot tool.

        Uses ``import`` (ImageMagick) or ``grim`` (Wayland) to capture
        a specific window region or the focused window.
        """
        if widget_path:
            accessible = self._resolve_path(widget_path)
            if accessible:
                comp = accessible.get_component_iface()
                if comp:
                    rect = comp.get_extents(Atspi.CoordType.SCREEN)
                    # Try grim first (Wayland), fall back to import (X11)
                    geometry = f"{rect.x},{rect.y} {rect.width}x{rect.height}"
                    try:
                        result = subprocess.run(
                            ["grim", "-g", geometry, "-"],
                            capture_output=True, check=True,
                        )
                        return result.stdout
                    except FileNotFoundError:
                        result = subprocess.run(
                            ["import", "-window", "root", "-crop",
                             f"{rect.width}x{rect.height}+{rect.x}+{rect.y}", "png:-"],
                            capture_output=True, check=True,
                        )
                        return result.stdout

        # Capture focused window
        try:
            result = subprocess.run(
                ["grim", "-"],
                capture_output=True, check=True,
            )
            return result.stdout
        except FileNotFoundError:
            result = subprocess.run(
                ["import", "-window", "root", "png:-"],
                capture_output=True, check=True,
            )
            return result.stdout
