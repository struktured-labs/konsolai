#!/usr/bin/env python3
"""Konsolai GUI Interaction Tests — validates UI behavior via AT-SPI.

These tests INTERACT with the running Konsolai instance (click buttons,
switch tabs, read state changes). Run against a live instance only:

    python3 Testing/gui-interaction-test.py

Requires: QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 on the Konsolai process.
WARNING: These tests click UI elements. Don't run during active work.
"""

from __future__ import annotations

import json
import re
import sys
import os
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools", "gui-mcp", "src"))

from konsolai_gui_mcp.atspi_backend import AtspiBackend
from konsolai_gui_mcp.types import WidgetNode

# ---------------------------------------------------------------------------
# Test infrastructure (same as smoke test)
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
# Interaction tests
# ---------------------------------------------------------------------------

def test_tab_click(backend: AtspiBackend):
    """Click a tab and verify it becomes selected."""
    print("\n[Tab Click Interaction]")
    tabs = backend.find_widget("konsolai", role="tab")
    check("at least 2 tabs for click test", len(tabs) >= 2)
    if len(tabs) < 2:
        return

    # Remember current tab
    original_tab = None
    for tab in tabs:
        try:
            state = backend.get_widget_state(tab.path)
            if state.selected:
                original_tab = tab
                break
        except Exception:
            pass

    # Find a different tab to click
    target_tab = None
    for tab in tabs:
        if tab.path != (original_tab.path if original_tab else ""):
            # Skip non-session tabs (Quick Commands etc)
            if "Quick Commands" in tab.path or "Warnings" in tab.path or "Command" in tab.path:
                continue
            target_tab = tab
            break

    check("found target tab to click", target_tab is not None)
    if not target_tab:
        return

    # Click it
    result = backend.click_widget(target_tab.path)
    check("tab click succeeded", result.success, result.message)

    # Wait for UI to update
    time.sleep(0.3)

    # Verify the window title changed (reflects active tab)
    tree = backend.get_widget_tree("konsolai", max_depth=1)
    if tree.children:
        window_title = tree.children[0].info.name
        # Tab name should appear in the window title
        target_name = target_tab.name.split(" (")[0]  # "fluxit (5a6f8ad3)" → "fluxit"
        check("window title reflects clicked tab", target_name.lower() in window_title.lower(),
              f"title='{window_title}', expected '{target_name}'")

    # Click back to original if we had one
    if original_tab:
        backend.click_widget(original_tab.path)
        time.sleep(0.2)


def test_status_bar_updates(backend: AtspiBackend):
    """Status bar should reflect the active session."""
    print("\n[Status Bar Updates]")
    # Read status bar text
    tree = backend.get_widget_tree("konsolai", max_depth=4)
    status_bar = find_child(tree, role="status_bar")
    check("status_bar found", status_bar is not None)
    if not status_bar:
        return

    labels = find_all(status_bar, role="label")
    status_labels = [l for l in labels if "Claude:" in l.info.name]
    check("status label present", len(status_labels) >= 1)

    if status_labels:
        text = status_labels[0].info.name
        # State is always present
        check("status bar has state",
              any(s in text for s in ["Idle", "Working", "Waiting", "Not Running", "Starting"]),
              f"text={text[:60]}")
        # Model/tokens/context are only present for Claude sessions, not bash tabs
        is_claude_tab = "Not Running" not in text
        if is_claude_tab:
            check("status bar has model", "opus" in text or "sonnet" in text or "haiku" in text,
                  f"text={text[:60]}")
            check("status bar has context", "Ctx:" in text, f"text={text[:60]}")
            check("status bar has tokens", "↑" in text, f"text={text[:60]}")
        else:
            print("  SKIP  Model/tokens/context (non-Claude tab active)")


def test_menu_structure(backend: AtspiBackend):
    """Verify Claude menu items exist with correct structure."""
    print("\n[Claude Menu Items]")
    tree = backend.get_widget_tree("konsolai", max_depth=5)
    claude_menu = find_child(tree, name="Claude")
    check("Claude menu found", claude_menu is not None)
    if not claude_menu:
        return

    popup = find_child(claude_menu, role="popup_menu")
    check("popup menu found", popup is not None)
    if not popup:
        return

    # Get all menu items
    menu_items = find_all(popup, role="menu_item")
    # Also check for check_menu_item (yolo toggles show as these)
    check_items = find_all(popup, role="check_menu_item")
    all_items = menu_items + check_items
    item_names = [i.info.name for i in all_items]

    # Expected actions
    for expected in ["Approve Permission", "Deny Permission", "Stop Claude", "Restart Claude"]:
        found = any(expected in name for name in item_names)
        check(f"menu has '{expected}'", found, f"items={[n for n in item_names if n]}")

    # Yolo modes (may be menu_item or check_menu_item depending on Qt version)
    for expected in ["Yolo Mode", "Double Yolo", "Triple Yolo"]:
        found = any(expected in name for name in item_names)
        check(f"menu has '{expected}'", found, f"items={[n for n in item_names if n]}")


def test_session_panel_interaction(backend: AtspiBackend):
    """Session panel tree should have expandable categories."""
    print("\n[Session Panel Interaction]")
    tree = backend.get_widget_tree("konsolai", max_depth=5)
    sessions_panel = find_child(tree, name="Sessions")
    check("Sessions panel found", sessions_panel is not None)
    if not sessions_panel:
        return

    # Use find_widget to locate the tree — it searches the full depth
    all_trees = backend.find_widget("konsolai", role="tree")
    # Filter to trees inside the Sessions panel (by path)
    session_trees = [t for t in all_trees if "Sessions" in t.path]
    session_tree_info = max(session_trees, key=lambda t: t.children_count) if session_trees else None
    check("session tree found", session_tree_info is not None,
          f"trees in Sessions: {len(session_trees)}")
    if not session_tree_info:
        return

    check("session tree has items", session_tree_info.children_count > 0,
          f"count={session_tree_info.children_count}")

    # Read tree item states
    tree_items = backend.find_widget("konsolai", role="tree_item")
    check("session tree has items", len(tree_items) > 0,
          f"count={len(tree_items)}")

    # Filter to items inside Sessions panel
    session_items = [t for t in tree_items if "Sessions" in t.path]
    check("session tree items found", len(session_items) > 0,
          f"count={len(session_items)}")

    if session_items:
        # Re-query to get fresh paths (window title may have changed from tab click)
        fresh_items = backend.find_widget("konsolai", role="tree_item")
        fresh_session = [t for t in fresh_items if "Sessions" in t.path]
        if fresh_session:
            first = fresh_session[0]
            try:
                state = backend.get_widget_state(first.path)
                check("tree item state readable", True)
                check("tree item is enabled", state.enabled)
                check("tree item is visible", state.visible)
            except Exception as e:
                check("tree item state readable", False, str(e))
        else:
            check("tree item state readable (re-query)", False, "no items after re-query")


def test_toolbar_button_states(backend: AtspiBackend):
    """Toolbar buttons should have correct enabled/disabled states."""
    print("\n[Toolbar Button States]")
    # Only test Main Toolbar buttons — Session Toolbar buttons have paths
    # that change with the window title (which changes on tab click).
    for name in ["New Tab"]:
        try:
            results = backend.find_widget("konsolai", role="button", name=name)
            # Filter to Main Toolbar only
            main_results = [r for r in results if "Main Toolbar" in r.path]
            if not main_results:
                main_results = results[:1]
            check(f"button '{name}' found", len(main_results) >= 1)
            if main_results:
                state = backend.get_widget_state(main_results[0].path)
                check(f"button '{name}' visible", state.visible)
        except (ValueError, Exception) as e:
            check(f"button '{name}' accessible", False, str(e))


def test_window_properties(backend: AtspiBackend):
    """Window should have correct accessible properties."""
    print("\n[Window Properties]")
    tree = backend.get_widget_tree("konsolai", max_depth=1)
    check("app root exists", tree is not None)
    if not tree.children:
        return

    window = tree.children[0]
    props = backend.get_widget_properties(window.info.path)
    check("window has properties", len(props) > 0)
    check("window role is frame/window", props.get("role", "") in ["frame", "window"],
          f"role={props.get('role')}")
    # States come from get_widget_state, not get_widget_properties
    try:
        state = backend.get_widget_state(window.info.path)
        check("window is visible", state.visible)
        check("window is enabled", state.enabled)
    except Exception as e:
        check("window state readable", False, str(e))


def test_multiple_tab_reads(backend: AtspiBackend):
    """Read all tab names — none should be empty or broken."""
    print("\n[Tab Name Validation]")
    tabs = backend.find_widget("konsolai", role="tab")
    check("tabs found", len(tabs) >= 1, f"count={len(tabs)}")

    empty_tabs = 0
    for tab in tabs:
        if not tab.name or tab.name.strip() == "":
            empty_tabs += 1
    check("no empty tab names", empty_tabs == 0, f"empty={empty_tabs}/{len(tabs)}")

    # Session tabs should have the format "name (id)" or "~ : bash"
    session_tabs = [t for t in tabs if "Quick Commands" not in t.path and "Warnings" not in t.path and "Command" not in t.path]
    check("session tabs have names", all(t.name for t in session_tabs),
          f"names={[t.name for t in session_tabs[:5]]}")


def test_worktree_prerequisites(backend: AtspiBackend):
    """Sessions must have working directories (needed for worktree action)."""
    print("\n[Worktree Session Prerequisites]")
    # The "New Worktree Session..." context menu action requires sessions
    # to have a working directory. Verify that session tree items exist
    # and that tabs have project names (indicating working dirs are set).
    tabs = backend.find_widget("konsolai", role="tab")
    session_tabs = [t for t in tabs
                    if "Quick Commands" not in t.path
                    and "Warnings" not in t.path
                    and "Command" not in t.path
                    and "bash" not in t.name]
    check("Claude session tabs exist", len(session_tabs) >= 1,
          f"count={len(session_tabs)}")

    # Session tabs with project names indicate working directory is set
    # Format: "project-name (sessionId)" e.g. "konsolai (06a8ac8b)"
    tabs_with_projects = [t for t in session_tabs if "(" in t.name]
    check("session tabs have project names", len(tabs_with_projects) >= 1,
          f"count={len(tabs_with_projects)}, names={[t.name for t in tabs_with_projects[:3]]}")

    # Verify session panel tree has items (worktree menu attaches to these)
    tree_items = backend.find_widget("konsolai", role="tree_item")
    session_items = [t for t in tree_items if "Sessions" in t.path]
    check("session tree items available for context menu", len(session_items) > 0,
          f"count={len(session_items)}")


# ---------------------------------------------------------------------------
# Runner
# ---------------------------------------------------------------------------

def main():
    print("Konsolai GUI Interaction Tests")
    print("=" * 50)

    backend = AtspiBackend()

    apps = backend.list_applications()
    if not any(a.name == "konsolai" for a in apps):
        print("\nERROR: konsolai not found in AT-SPI tree.")
        print("Make sure konsolai is running with QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1")
        sys.exit(2)

    # Non-mutating tests first (don't change window title/active tab)
    test_status_bar_updates(backend)
    test_menu_structure(backend)
    test_session_panel_interaction(backend)
    test_toolbar_button_states(backend)
    test_window_properties(backend)
    test_multiple_tab_reads(backend)
    test_worktree_prerequisites(backend)
    # Tab click LAST — changes window title, invalidates cached widget paths
    test_tab_click(backend)

    print("\n" + "=" * 50)
    print(f"Results: {_passed} passed, {_failed} failed")
    if _errors:
        print("\nFailures:")
        for e in _errors:
            print(f"  - {e}")
    sys.exit(1 if _failed > 0 else 0)


if __name__ == "__main__":
    main()
