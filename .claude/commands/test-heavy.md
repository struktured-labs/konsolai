Run the full Konsolai test suite (all 65 tests, including upstream Konsole tests).

```
bash /home/struktured/projects/konsolai/tools/gate.sh
```
then, for the upstream tests the light filter excludes:
```
ctest --test-dir /home/struktured/projects/konsolai/build --output-on-failure --timeout 120
```

Runtime measured 2026-07-29: **~61 seconds** for the whole suite, single-threaded.
The slowest are `PartTest` 10.6s, `TerminalInterfaceTest` 10.2s, `ProcessInfoTest` 9.3s.

Two corrections to what this file used to say, both measured:
- It claimed 5–6 minutes. It is ~61s.
- It claimed `ViewManagerTest` is a known SIGSEGV that hangs 300+ seconds and should
  be excluded from the verdict. **It passes in 0.42s.** Whatever caused that was
  fixed; do not pre-excuse its failure. If it fails now, that is real.

`appstreamtest` is registered twice (as Test #1 and Test #65) — a CMake duplication,
harmless, but it means `ctest -N` reports 65 tests across 64 distinct names.

Report: how many passed, how many failed, and details on every failure. There is no
known-bad test in this suite to discount, other than the `TokenTrackingTest`
file-watcher flake that `tools/gate.sh` carves out explicitly (cause unknown; it
fails intermittently in the full binary and passes standalone).
