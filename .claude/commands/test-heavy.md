Run the full Konsolai test suite (all tests including upstream Konsole tests).

WARNING: This suite includes slow tests (PartTest ~10s, ProcessInfoTest ~9s, TerminalInterfaceTest ~10s) and ViewManagerTest which is known to SIGSEGV/hang for 300+ seconds. Total runtime: 5-6 minutes.

Run with a generous timeout:
```
ctest --test-dir /home/struktured/projects/konsolai/build --output-on-failure --timeout 120
```

Note: ViewManagerTest (#20) is a known upstream SIGSEGV — its failure should be reported but is not a blocker for Konsolai changes.

Report the results: how many passed, how many failed, and details on any unexpected failures (excluding ViewManagerTest).
