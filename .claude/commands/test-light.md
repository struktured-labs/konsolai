Run the lightweight Konsolai test suite — the MANDATORY GATE after every change.

Run exactly this:
```
bash /home/struktured/projects/konsolai/tools/gate.sh
```

Report: the `GATE:` line verbatim, the pass/fail counts, and any failure details.

**Do not put a `-R` filter in this file.** It used to carry its own copy, which
drifted 14 terms behind `tools/gate.sh` — selecting 31 tests where the gate selects
45, silently skipping `CodexProcessTest`, `KonsolaiSettingsTest`, `LettaApiClientTest`,
the Merge / Broadcast / Reorganize / TreeToolbar / SessionTreeWidget suites, and the
four konsolai tests no prefix matched at all. Features shipped, their tests were
registered in CMake, and nothing updated the copy here — so the gate everyone was
told to run had not covered them for months.

`tools/gate.sh` is the single source of truth for what "light" means. It also does
things a bare `ctest` cannot: it builds first (ctest never compiles, so a raw run
reports on the previous build), refuses when zero tests match, refuses a `*Test.cpp`
that CMake does not register, and ratchets a floor so the suite cannot silently
shrink. A bare `ctest` exits 0 on a typo'd filter and on an unconfigured build dir.
