"""Tests for transcript parsing."""

from __future__ import annotations

from konsolai_bridge.transcript_parser import parse_transcript, strip_ansi


def test_strip_ansi():
    raw = "\x1b[32mgreen\x1b[0m normal"
    assert strip_ansi(raw) == "green normal"


def test_parse_simple_transcript():
    raw = """❯ hello
Hello! How can I help you today?

❯ fix the bug
I'll look into that bug now.
"""
    result = parse_transcript(raw, "test-session")
    assert result.session_name == "test-session"
    assert len(result.messages) >= 2
    # First message should be user
    user_msgs = [m for m in result.messages if m.role == "user"]
    assert len(user_msgs) >= 1
    assert user_msgs[0].content == "hello"


def test_parse_empty_transcript():
    result = parse_transcript("", "test-session")
    assert result.session_name == "test-session"
    assert len(result.messages) == 0


def test_raw_preserved():
    raw = "some raw output"
    result = parse_transcript(raw, "test-session")
    assert result.raw == raw
