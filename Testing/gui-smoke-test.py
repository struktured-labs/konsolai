#!/usr/bin/env python3
"""Konsolai GUI Smoke Test — validates UI state via AT-SPI MCP tools.

Run against a live Konsolai instance:
    python3 Testing/gui-smoke-test.py

Requires: QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 on the Konsolai process.
Uses the same backend as the MCP server (no MCP server needed to run).
"""

from __future__ import annotations

import json
import re
import sys
import os

# Add the MCP server source to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "gui-mcp", "src"))

from konsolai_gui_mcp.atspi_backend import AtspiBackend
from konsolai_gui_mcp.types import WidgetNode

# ---------------------------------------------------------------------------
# Test infrastructure
# ---------------------------------------------------------------------------

_passed = 0
_failed = 0
_errors: list[str] = []


def check(name: str, condition: bool, detail: str = ""):
    global _passed, _failed
    if condition:
        _passed += 1
        print(f"  PASS  {name}")
    else:
        _failed += 1
        msg = f"  FAIL  {name}"
        if detail:
            msg += f" — {detail}"
        print(msg)
        _errors.append(f"{name}: {detail}" if detail else name)


def find_child(node: WidgetNode, role: str = "", name: str = "") -> WidgetNode | None:
    """Recursively find a child node by role and/or name substring."""
    if role and node.info.role != role:
        pass
    elif name and name.lower() not in node.info.name.lower():
        pass
    elif role or name:
        return node
    for child in node.children:
        result = find_child(child, role, name)
        if result:
            return result
    return None


def find_all(node: WidgetNode, role: str = "", name: str = "") -> list[WidgetNode]:
    """Recursively find all children matching role and/or name."""
    results: list[WidgetNode] = []
    matches = True
    if role and node.info.role != role:
        matches = False
    if name and name.lower() not in node.info.name.lower():
        matches = False
    if matches and (role or name):
        results.append(node)
    for child in node.children:
        results.extend(find_all(child, role, name))
    return results


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_app_visible(backend: AtspiBackend):
    """Konsolai must be visible in the AT-SPI application list."""
    print("\n[Application Visibility]")
    apps = backend.list_applications()
    app_names = [a.name for a in apps]
    check("konsolai in app list", "konsolai" in app_names,
          f"found: {app_names}")
    konsolai = [a for a in apps if a.name == "konsolai"]
    if konsolai:
        check("konsolai PID > 0", konsolai[0].pid > 0, f"pid={konsolai[0].pid}")
        check("konsolai toolkit is Qt", konsolai[0].toolkit == "Qt", f"toolkit={konsolai[0].toolkit}")


def test_window_structure(backend: AtspiBackend):
    """Main window must have expected top-level children."""
    print("\n[Window Structure]")
    tree = backend.get_widget_tree("konsolai", max_depth=2)

    check("application root exists", tree.info.role == "application")
    check("has at least 1 window", len(tree.children) >= 1)

    if not tree.children:
        return

    window = tree.children[0]
    check("window role correct", window.info.role == "window")
    check("window title contains Konsolai", "Konsolai" in window.info.name,
          f"title={window.info.name}")

    child_roles = [c.info.role for c in window.children]
    child_names = [c.info.name for c in window.children]

    check("menu_bar present", "menu_bar" in child_roles)
    check("status_bar present", "status_bar" in child_roles)
    check("toolbar present", "toolbar" in child_roles)
    check("Sessions panel present", "Sessions" in child_names)


def test_menu_bar(backend: AtspiBackend):
    """Menu bar must contain all expected menus including Claude."""
    print("\n[Menu Bar]")
    tree = backend.get_widget_tree("konsolai", max_depth=3)
    menu_bar = find_child(tree, role="menu_bar")
    check("menu_bar found", menu_bar is not None)
    if not menu_bar:
        return

    menu_names = [c.info.name for c in menu_bar.children if c.info.role == "menu_item"]
    for expected in ["File", "Edit", "View", "Claude", "Settings", "Help"]:
        check(f"menu '{expected}' exists", expected in menu_names,
              f"menus={menu_names}")


def test_claude_menu(backend: AtspiBackend):
    """Claude menu must have the correct number of items."""
    print("\n[Claude Menu]")
    tree = backend.get_widget_tree("konsolai", max_depth=4)
    claude_menu = find_child(tree, name="Claude")
    check("Claude menu item found", claude_menu is not None)
    if not claude_menu:
        return

    # The popup menu child contains the actual menu items
    popup = find_child(claude_menu, role="popup_menu")
    check("Claude popup menu exists", popup is not None)
    if popup:
        check("Claude menu has items", popup.info.children_count >= 10,
              f"items={popup.info.children_count}")


def test_status_bar(backend: AtspiBackend):
    """Status bar must show Claude state, model, and context percent."""
    print("\n[Status Bar]")
    tree = backend.get_widget_tree("konsolai", max_depth=4)
    status_bar = find_child(tree, role="status_bar")
    check("status_bar found", status_bar is not None)
    if not status_bar:
        return

    # Find the status label (contains "Claude:")
    labels = find_all(status_bar, role="label")
    status_labels = [l for l in labels if "Claude:" in l.info.name]
    check("Claude status label found", len(status_labels) >= 1,
          f"labels={[l.info.name[:50] for l in labels]}")

    if status_labels:
        text = status_labels[0].info.name
        check("shows state", any(s in text for s in ["Idle", "Working", "Waiting", "Not Running"]),
              f"text={text[:80]}")
        check("shows model name", "opus" in text or "sonnet" in text or "haiku" in text,
              f"text={text[:80]}")
        check("shows Ctx: percent", "Ctx:" in text, f"text={text[:80]}")


def test_tabs(backend: AtspiBackend):
    """Tab bar must have at least one tab."""
    print("\n[Tab Bar]")
    tabs = backend.find_widget("konsolai", role="tab")
    check("at least 1 tab exists", len(tabs) >= 1, f"count={len(tabs)}")

    tab_list = backend.find_widget("konsolai", role="tab_list")
    check("tab_list widget exists", len(tab_list) >= 1)


def test_session_panel(backend: AtspiBackend):
    """Session panel must contain a tree widget with session entries."""
    print("\n[Session Panel]")
    tree = backend.get_widget_tree("konsolai", max_depth=4)
    sessions = find_child(tree, name="Sessions")
    check("Sessions panel found", sessions is not None)
    if not sessions:
        return

    session_tree = find_child(sessions, role="tree")
    check("session tree widget found", session_tree is not None)
    if session_tree:
        check("session tree has entries", session_tree.info.children_count > 0,
              f"count={session_tree.info.children_count}")


def test_toolbars(backend: AtspiBackend):
    """Main and Session toolbars must exist with expected buttons."""
    print("\n[Toolbars]")
    tree = backend.get_widget_tree("konsolai", max_depth=4)

    main_tb = find_child(tree, name="Main Toolbar")
    check("Main Toolbar found", main_tb is not None)
    if main_tb:
        new_tab = find_child(main_tb, name="New Tab")
        check("New Tab button in Main Toolbar", new_tab is not None)

    session_tb = find_child(tree, name="Session Toolbar")
    check("Session Toolbar found", session_tb is not None)
    if session_tb:
        copy_btn = find_child(session_tb, name="Copy")
        paste_btn = find_child(session_tb, name="Paste")
        check("Copy button in Session Toolbar", copy_btn is not None)
        check("Paste button in Session Toolbar", paste_btn is not None)


def test_widget_state(backend: AtspiBackend):
    """Key widgets must be in expected states."""
    print("\n[Widget States]")
    buttons = backend.find_widget("konsolai", role="button", name="New Tab")
    check("New Tab button found", len(buttons) >= 1)
    if buttons:
        state = backend.get_widget_state(buttons[0].path)
        check("New Tab button enabled", state.enabled)
        check("New Tab button visible", state.visible)


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    print("Konsolai GUI Smoke Test")
    print("=" * 50)

    backend = AtspiBackend()

    # Check konsolai is running
    apps = backend.list_applications()
    if not any(a.name == "konsolai" for a in apps):
        print("\nERROR: konsolai not found in AT-SPI tree.")
        print("Make sure konsolai is running with QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1")
        sys.exit(2)

    test_app_visible(backend)
    test_window_structure(backend)
    test_menu_bar(backend)
    test_claude_menu(backend)
    test_status_bar(backend)
    test_tabs(backend)
    test_session_panel(backend)
    test_toolbars(backend)
    test_widget_state(backend)

    print("\n" + "=" * 50)
    print(f"Results: {_passed} passed, {_failed} failed")
    if _errors:
        print("\nFailures:")
        for e in _errors:
            print(f"  - {e}")
    sys.exit(1 if _failed > 0 else 0)


if __name__ == "__main__":
    main()
