#!/usr/bin/env python3
"""GUI test: "Restart All Live" on category and project-group menus, end to end.

Run via Testing/run-restart-live-gui-test.sh, which seeds a sandboxed
instance (own HOME, tmux server, and a fake `claude`) with category "cowir":
alpha x2 live at launch (reattached into tabs) and beta, whose tmux this test
starts after launch so it is live with no tab.

Opens each context menu with a real right-click (the tree handles right-click
in a viewport event filter, so AT-SPI actions can't reach it), reads it, then
runs the category restart end to end and checks every pane was respawned
resuming its OWN conversation — including the two sharing a workdir.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib  # noqa: E402

_passed = 0
_failed = 0


def check(name: str, condition: bool, detail: str = "") -> None:
    global _passed, _failed
    if condition:
        _passed += 1
        print(f"  PASS  {name}")
    else:
        _failed += 1
        print(f"  FAIL  {name}" + (f" — {detail}" if detail else ""))


def find_app(pid: int):
    desktop = Atspi.get_desktop(0)
    for i in range(desktop.get_child_count()):
        app = desktop.get_child_at_index(i)
        try:
            if app and app.get_process_id() == pid:
                return app
        except Exception:
            pass
    return None


def walk(node, depth=0, max_depth=40):
    if node is None or depth > max_depth:
        return
    yield node
    try:
        n = node.get_child_count()
    except Exception:
        return
    for i in range(n):
        try:
            child = node.get_child_at_index(i)
        except Exception:
            continue
        yield from walk(child, depth + 1, max_depth)


def find_by(app, role_names: tuple[str, ...], predicate):
    for node in walk(app):
        try:
            if node.get_role_name() in role_names and predicate(node.get_name() or ""):
                return node
        except Exception:
            continue
    return None


def wait_for(fn, timeout: float, interval: float = 0.5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = fn()
        if result:
            return result
        time.sleep(interval)
    return None


TREE_ROLES = ("tree item", "table cell", "list item")
MENU_ITEM_ROLES = ("menu item", "check menu item")


def right_click(node) -> bool:
    try:
        comp = node.get_component_iface()
        if comp is None:
            return False
        r = comp.get_extents(Atspi.CoordType.SCREEN)
    except GLib.GError:
        return False  # the tree rebuilt and this item no longer exists
    if r.width <= 0 or r.height <= 0:
        return False
    x, y = r.x + min(40, r.width // 2), r.y + r.height // 2
    subprocess.run(["xdotool", "mousemove", str(x), str(y)], check=True)
    time.sleep(0.2)
    subprocess.run(["xdotool", "click", "3"], check=True)
    return True


class FocusWatcher:
    """Collects accessibles that gain focus.

    Qt's AT-SPI bridge leaves Qt::Popup windows out of the application's
    children, so an open context menu can't be found by walking the tree.
    Its items still emit focus events: arrow into the menu, catch the focused
    item, and read the menu through item.get_parent().
    """

    def __init__(self):
        self.focused = []
        self._listener = Atspi.EventListener.new(self._on_event)
        self._listener.register("object:state-changed:focused")

    def _on_event(self, event):
        try:
            if event.detail1 == 1 and event.source.get_role_name() in MENU_ITEM_ROLES:
                self.focused.append(event.source)
        except Exception:
            pass

    def pump(self, seconds: float):
        ctx = GLib.MainContext.default()
        deadline = time.time() + seconds
        while time.time() < deadline:
            while ctx.pending():
                ctx.iteration(False)
            time.sleep(0.05)


def menu_of(watcher: FocusWatcher, node):
    """Right-click node and return its open context menu's accessible."""
    if not right_click(node):
        return None
    watcher.focused.clear()
    watcher.pump(1.0)
    subprocess.run(["xdotool", "key", "Down"], check=True)
    watcher.pump(1.5)
    return watcher.focused[-1].get_parent() if watcher.focused else None


def open_menu_and_read(watcher: FocusWatcher, node) -> list[str]:
    """Right-click node, return the names of the context menu's items."""
    menu = menu_of(watcher, node)
    names = []
    if menu is not None:
        for i in range(menu.get_child_count()):
            child = menu.get_child_at_index(i)
            if child is not None and child.get_name():
                names.append(child.get_name())
    subprocess.run(["xdotool", "key", "Escape"], check=True)
    watcher.pump(0.5)
    return names or ["<no menu item took focus — did the menu open?>"]


def do_default_action(node) -> bool:
    """Press/click/toggle the node — by action NAME: Qt lists SetFocus too,
    and index 0 is not always the press."""
    action = node.get_action_iface() if node is not None else None
    if action is None or action.get_n_actions() == 0:
        return False
    names = [action.get_action_name(i).lower() for i in range(action.get_n_actions())]
    index = next((i for i, n in enumerate(names) if n in ("press", "click", "activate", "toggle")), 0)
    try:
        action.do_action(index)
    except GLib.GError:
        pass  # a slow reply is not a failed press; callers verify the effect
    return True


def full_text(node) -> str:
    """A label's accessible name can stop at the first newline; the text
    interface has all of it."""
    try:
        text = node.get_text_iface()
        if text is not None:
            return text.get_text(0, text.get_character_count())
    except GLib.GError:
        pass
    return node.get_name() or ""


def drop_tree_focus(app):
    """Tree rebuilds are skipped while the window is inactive and deferred
    while the tree is hovered or focused. With no window manager X focus
    follows the pointer, so park it INSIDE the window, on the terminal area
    right of the tree, and click there to move keyboard focus off the tree."""
    fr = app.get_child_at_index(0).get_component_iface().get_extents(Atspi.CoordType.SCREEN)
    x, y = fr.x + fr.width * 3 // 4, fr.y + fr.height // 2
    subprocess.run(["xdotool", "mousemove", str(x), str(y), "click", "1"], check=True)
    time.sleep(0.3)


def pane_start_commands(sock: str) -> dict[str, str]:
    out = subprocess.run(["tmux", "-S", sock, "list-panes", "-a", "-F", "#{session_name}\t#{pane_start_command}"],
                         capture_output=True, text=True).stdout
    return dict(line.split("\t", 1) for line in out.splitlines() if "\t" in line)


def session_attached(sock: str, name: str) -> int:
    out = subprocess.run(["tmux", "-S", sock, "display-message", "-p", "-t", name, "#{session_attached}"],
                         capture_output=True, text=True).stdout.strip()
    return int(out) if out.isdigit() else 0


def find_category(app):
    # Re-find on every use: tree rebuilds replace the items, and an old
    # accessible's object path is gone.
    return wait_for(lambda: find_by(app, TREE_ROLES, lambda n: n.startswith("cowir (")), timeout=10)


def run_end_to_end(app, watcher: FocusWatcher) -> None:
    print("\n[Restart All Live — end to end]")
    sock = os.environ["RESTART_TEST_SOCK"]
    expected = {
        "konsolai-guitest-Claude-a1a1a1a1": os.environ["CONV_A1"],
        "konsolai-guitest-Claude-a2a2a2a2": os.environ["CONV_A2"],
        "konsolai-guitest-Claude-b1b1b1b1": os.environ["CONV_B1"],
    }
    before = pane_start_commands(sock)

    # Bring beta up now: live, but konsolai has no tab for it.
    subprocess.run(["tmux", "-S", sock, "new-session", "-d", "-s", "konsolai-guitest-Claude-b1b1b1b1", "-c",
                    os.environ["RESTART_TEST_BETA"], f"FAKE_CONVERSATION_ID={expected['konsolai-guitest-Claude-b1b1b1b1']} claude"],
                   check=True)

    # Live names refresh on tree rebuilds; toggling a filter chip forces one.
    menu = None
    for _attempt in range(4):
        drop_tree_focus(app)
        chip = find_by(app, ("toggle button", "push button", "check box"), lambda n: n == "Pinned")
        if chip is not None:
            do_default_action(chip)
            time.sleep(0.3)
            do_default_action(chip)
        time.sleep(2.0)
        menu = menu_of(watcher, find_category(app))
        names = [menu.get_child_at_index(i).get_name() for i in range(menu.get_child_count())] if menu else []
        if "Restart All Live in Category (3)" in names:
            break
        alive = subprocess.run(["tmux", "-S", sock, "has-session", "-t", "konsolai-guitest-Claude-b1b1b1b1"]).returncode == 0
        print(f"    attempt {_attempt}: chip={'found' if chip is not None else 'MISSING'} beta_tmux_alive={alive} "
              f"restart_items={[n for n in names if n and n.startswith('Restart')]}")
        subprocess.run(["xdotool", "key", "Escape"], check=True)
        watcher.pump(0.5)
        menu = None
    check("tabless beta counted once live: 'Restart All Live in Category (3)'", menu is not None)
    if menu is None:
        return

    # Activate by keyboard. An AT-SPI do_action here never returns: the slot
    # opens a modal dialog, and the reply waits on its nested event loop.
    focused_name = ""
    for _step in range(40):
        focused_name = watcher.focused[-1].get_name() if watcher.focused else ""
        if focused_name == "Restart All Live in Category (3)":
            break
        subprocess.run(["xdotool", "key", "Down"], check=True)
        watcher.pump(0.3)
    check("reached the restart action by keyboard", focused_name == "Restart All Live in Category (3)", focused_name)
    subprocess.run(["xdotool", "key", "Return"], check=True)

    yes = wait_for(lambda: find_by(app, ("push button", "button"), lambda n: n.replace("&", "") == "Yes"), timeout=10)
    check("confirmation dialog shown", yes is not None)
    if yes is None:
        print(f"    panes now: {pane_start_commands(sock)}")
        for i in range(app.get_child_count()):
            w = app.get_child_at_index(i)
            print(f"    top-level[{i}]: role={w.get_role_name()!r} name={w.get_name()!r}")
            for n in walk(w, max_depth=6):
                if "button" in n.get_role_name() or n.get_role_name() == "label":
                    print(f"      {n.get_role_name()}: {n.get_name()!r}")
        return
    prompt = find_by(app, ("label",), lambda n: n.startswith('Restart 3 live sessions in "cowir"?'))
    check("dialog names the scope without its count", prompt is not None)
    prompt_text = full_text(prompt) if prompt is not None else ""
    check("dialog says the tabless one is reopened first", "1 of them has no open tab; it will be reopened first." in prompt_text, prompt_text)
    do_default_action(yes)

    def all_respawned():
        cmds = pane_start_commands(sock)
        done = all(cmds.get(name, "") != before.get(name) and "--resume " in cmds.get(name, "") for name in expected)
        return cmds if done else None

    cmds = wait_for(all_respawned, timeout=30) or pane_start_commands(sock)
    for name, conv in expected.items():
        cmd = cmds.get(name, "")
        short = name.rsplit("-", 1)[-1]
        check(f"{short} respawned resuming its own conversation", f"--resume {conv}" in cmd, cmd)
        others = [c for n, c in expected.items() if n != name]
        check(f"{short} not resumed onto another session's conversation", not any(o in cmd for o in others), cmd)
    check("stale saved ID lost to the live CLI record", "00000000-dead" not in cmds.get("konsolai-guitest-Claude-b1b1b1b1", ""))
    check("tabless beta was reopened in a tab", session_attached(sock, "konsolai-guitest-Claude-b1b1b1b1") > 0)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: gui-restart-live-test.py <konsolai-pid>")
        return 1
    pid = int(sys.argv[1])

    print("\n[Restart All Live — context menus]")
    app = wait_for(lambda: find_app(pid), timeout=40)
    check("konsolai visible over AT-SPI", app is not None)
    if app is None:
        return 1

    # Startup reattaches alpha's two live sessions into tabs, and each one
    # rebuilds the tree. Wait for both, then let the rebuilds settle, so a
    # right-click lands on the row it was aimed at.
    sock = os.environ["RESTART_TEST_SOCK"]
    alpha = ("konsolai-guitest-Claude-a1a1a1a1", "konsolai-guitest-Claude-a2a2a2a2")
    reattached = wait_for(lambda: all(session_attached(sock, n) > 0 for n in alpha), timeout=40)
    check("startup reattached alpha's live sessions into tabs", bool(reattached))
    time.sleep(2.0)

    category = find_category(app)
    check("category 'cowir' rendered", category is not None)
    if category is None:
        return 1

    watcher = FocusWatcher()
    names = open_menu_and_read(watcher, category)
    check("category menu offers 'Restart All Live in Category (2)'", "Restart All Live in Category (2)" in names, f"items={names}")

    # Reveal the project group under the category, then check its menu.
    action = category.get_action_iface()
    if action is not None and not category.get_state_set().contains(Atspi.StateType.EXPANDED):
        for i in range(action.get_n_actions()):
            if "expand" in action.get_action_name(i).lower():
                action.do_action(i)
                break
    group = wait_for(lambda: find_by(app, TREE_ROLES, lambda n: n.startswith("alpha (")), timeout=10)
    check("project group 'alpha' rendered", group is not None)
    if group is not None:
        names = open_menu_and_read(watcher, group)
        check("group menu offers 'Restart All Live in Group (2)'", "Restart All Live in Group (2)" in names, f"items={names}")
        check("old active-only label is gone", not any(n.startswith("Restart All Active in Group") for n in names), f"items={names}")

    run_end_to_end(app, watcher)

    print(f"\n  {_passed} passed, {_failed} failed")
    return 0 if _failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
