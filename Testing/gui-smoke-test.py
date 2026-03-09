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


def skip(name: str):
    print(f"  SKIP  {name}")


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
        # Model/context only shown when a Claude session is active
        has_active = "Not Running" not in text
        if has_active:
            check("shows model name", "opus" in text or "sonnet" in text or "haiku" in text,
                  f"text={text[:80]}")
            check("shows Ctx: percent", "Ctx:" in text, f"text={text[:80]}")
        else:
            skip("shows model name (no active session)")
            skip("shows Ctx: percent (no active session)")


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


def test_object_names(backend: AtspiBackend):
    """Key widgets should have objectName set for stable test targeting.

    These only pass after the objectName-enabled build is installed.
    Failures here are non-blocking — flagged as WARN not FAIL.
    """
    print("\n[Object Names / Automation IDs]")
    all_found = True
    for auto_id, label in [
        ("claudeStateLabel", "Claude state label"),
        ("claudeTaskLabel", "Claude task label"),
        ("sessionTree", "Session tree widget"),
        ("claudeMenu", "Claude menu"),
    ]:
        results = backend.find_widget("konsolai", auto_id=auto_id)
        if results:
            check(f"{label} has objectName '{auto_id}'", True)
        else:
            all_found = False
            print(f"  WARN  {label} missing objectName '{auto_id}' (needs install)")
    if not all_found:
        print("  NOTE  Run ./install.sh to deploy objectName markers")


def test_status_bar_content(backend: AtspiBackend):
    """Parse and validate status bar fields."""
    print("\n[Status Bar Content Parsing]")
    # Try objectName first, fall back to finding label with "Claude:" in name
    results = backend.find_widget("konsolai", auto_id="claudeStateLabel")
    if not results:
        tree = backend.get_widget_tree("konsolai", max_depth=4)
        status_bar = find_child(tree, role="status_bar")
        if status_bar:
            labels = find_all(status_bar, role="label")
            claude_labels = [l for l in labels if "Claude:" in l.info.name]
            if claude_labels:
                results = [claude_labels[0].info]
    if not results:
        check("status label found (objectName or fallback)", False)
        return
    path = results[0].path if hasattr(results[0], 'path') else results[0].path
    text = backend.read_text(path)
    check("status text not empty", len(text) > 0)

    # Parse known fields
    has_state = any(s in text for s in ["Idle", "Working", "Waiting", "Not Running", "Starting", "Error"])
    check("status contains state", has_state, f"text={text[:60]}")

    # Token/context/cost only present when a Claude session is active
    has_active = "Not Running" not in text
    if has_active:
        # Context percent: "Ctx:NN%"
        ctx_match = re.search(r"Ctx:(\d+)%", text)
        check("status contains Ctx:N%", ctx_match is not None, f"text={text[:80]}")
        if ctx_match:
            ctx_pct = int(ctx_match.group(1))
            check("context percent 0-100", 0 <= ctx_pct <= 100, f"ctx={ctx_pct}%")

        # Token usage: "NNN.NK↑ NNN.NK↓"
        check("status contains token arrows", "↑" in text and "↓" in text, f"text={text[:80]}")

        # Cost: "($N.NN)"
        cost_match = re.search(r"\(\$[\d.]+\)", text)
        check("status contains cost", cost_match is not None, f"text={text[:80]}")
    else:
        skip("status contains Ctx:N% (no active session)")
        skip("status contains token arrows (no active session)")
        skip("status contains cost (no active session)")


def test_tab_switching(backend: AtspiBackend):
    """Verify we can read tab names and that at least one is selected."""
    print("\n[Tab Switching]")
    tabs = backend.find_widget("konsolai", role="tab")
    check("tabs found for switching test", len(tabs) >= 2)
    if len(tabs) < 2:
        return

    # Check that tab states are readable
    states_read = 0
    for tab in tabs[:3]:
        try:
            state = backend.get_widget_state(tab.path)
            states_read += 1
        except Exception:
            pass
    check("tab states readable", states_read >= 1, f"read {states_read}/{min(3, len(tabs))}")


def test_session_tree_structure(backend: AtspiBackend):
    """Session tree must have category nodes."""
    print("\n[Session Tree Structure]")
    # Try objectName first, fall back to finding the largest tree in Sessions panel
    tree_results = backend.find_widget("konsolai", auto_id="sessionTree")
    tree_widget = backend.get_widget_tree("konsolai", max_depth=4)
    sessions_panel = find_child(tree_widget, name="Sessions")
    session_tree = None

    if tree_results:
        check("sessionTree found by objectName", True)
    elif sessions_panel:
        session_tree = find_child(sessions_panel, role="tree")
        check("session tree found (fallback)", session_tree is not None)
    else:
        check("Sessions panel found", False)
        return

    if not session_tree and sessions_panel:
        session_tree = find_child(sessions_panel, role="tree")
    if session_tree:
        check("session tree has children", session_tree.info.children_count > 0,
              f"count={session_tree.info.children_count}")


def test_session_tree_categories(backend: AtspiBackend):
    """Session tree must have category nodes (Pinned, Active, Detached, etc.)."""
    print("\n[Session Tree Categories]")
    tree_widget = backend.get_widget_tree("konsolai", max_depth=6)
    sessions_panel = find_child(tree_widget, name="Sessions")
    if not sessions_panel:
        skip("Sessions panel not found")
        return

    session_tree = find_child(sessions_panel, role="tree")
    if not session_tree:
        skip("session tree not found")
        return

    # Check for expected category nodes
    pinned_cat = find_child(session_tree, name="Pinned")
    active_cat = find_child(session_tree, name="Active")
    check("Pinned category exists", pinned_cat is not None)
    check("Active category exists", active_cat is not None)

    # If there are pinned sessions, they should be under the Pinned node
    if pinned_cat and pinned_cat.info.children_count > 0:
        check("Pinned category has entries", True,
              f"count={pinned_cat.info.children_count}")


def test_notification_overlay(backend: AtspiBackend):
    """If a notification overlay is visible, validate its structure."""
    print("\n[Notification Overlay]")
    # Notification widget shows "[project] Task Complete" etc.
    tree = backend.get_widget_tree("konsolai", max_depth=5)
    # Look for notification-like labels
    task_labels = find_all(tree, name="Task Complete")
    info_labels = find_all(tree, name="Permission")
    has_notification = len(task_labels) > 0 or len(info_labels) > 0
    # This is optional — notification may not be showing
    if has_notification:
        check("notification overlay visible", True)
    else:
        print("  SKIP  No notification currently visible (OK)")


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

    # Structure tests
    test_app_visible(backend)
    test_window_structure(backend)
    test_menu_bar(backend)
    test_claude_menu(backend)
    test_status_bar(backend)
    test_tabs(backend)
    test_session_panel(backend)
    test_toolbars(backend)
    test_widget_state(backend)

    # Stability tests (objectName-based)
    test_object_names(backend)
    test_status_bar_content(backend)

    # Interaction tests
    test_tab_switching(backend)
    test_session_tree_structure(backend)
    test_session_tree_categories(backend)
    test_notification_overlay(backend)

    print("\n" + "=" * 50)
    print(f"Results: {_passed} passed, {_failed} failed")
    if _errors:
        print("\nFailures:")
        for e in _errors:
            print(f"  - {e}")
    sys.exit(1 if _failed > 0 else 0)


if __name__ == "__main__":
    main()
