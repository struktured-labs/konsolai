Run the lightweight Konsolai test suite (Claude integration tests only).

These tests are fast (<30s total) and safe to run frequently during development. They cover all Claude-specific functionality without the slow upstream Konsole tests.

Run this command from the build directory:
```
ctest --test-dir /home/struktured/projects/konsolai/build --output-on-failure -R "Claude|Tmux|Token|Budget|SessionManager|SessionObserver|Agent|Notification|ProfileClaude|Resource|Prompt|OneShot|Keyboard|TabIndicator|StatusWidget"
```

Report the results: how many passed, how many failed, and any failure details.
