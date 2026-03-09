# Testing Guide

## Test Requirements

Every feature, bug fix, or behavioral change MUST have automated tests:

1. **C++ unit tests** (`src/autotests/`) — logic, data structures, provider parsing, signal verification
2. **GUI tests** (`Testing/`) — AT-SPI widget presence, tree node rendering, context menu items

A feature is NOT complete until both test types exist and pass.

## Running Tests

### Unit Tests

```bash
# Build
cd build && ninja -j4

# Run all tests
ctest --test-dir build/ --output-on-failure

# Run specific test groups
ctest --test-dir build/ -R "AgentFleetProvider|AgentManager"  # Agent tests
ctest --test-dir build/ -R "Yolo|ClaudeSessionYolo"           # Yolo tests
ctest --test-dir build/ -R "SessionManagerPanel"              # Session panel tests
```

### GUI Tests

```bash
# AT-SPI smoke tests (requires running Konsolai instance)
bash Testing/run-all-gui-tests.sh

# Isolated GUI tests (launches fresh instance)
bash Testing/run-isolated-gui-tests.sh
```

## Test Patterns

### Non-GUI Tests (QTEST_GUILESS_MAIN)

For logic, parsing, state machines:

```cpp
class MyTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void testSomething();
};
QTEST_GUILESS_MAIN(MyTest)
```

### Widget Tests (QTEST_MAIN)

For tests that create QWidget instances:

```cpp
QTEST_MAIN(MyWidgetTest)
// Link with: ${KONSOLAI_CLAUDE_TEST_LIBS} Qt::Widgets
```

### Test Libraries

```cmake
# Non-widget Claude tests
set(KONSOLAI_CLAUDE_TEST_LIBS Qt::Test Qt::Network konsolai_claude konsoleprivate)

# Widget tests add Qt::Widgets
LINK_LIBRARIES ${KONSOLAI_CLAUDE_TEST_LIBS} Qt::Widgets
```

## Test Organization

| Directory | Purpose |
|-----------|---------|
| `src/autotests/` | C++ unit tests (QTest framework) |
| `Testing/gui-smoke-test.py` | AT-SPI widget presence tests |
| `Testing/gui-interaction-test.py` | AT-SPI interaction tests |
| `Testing/gui-lifecycle-test.py` | Session lifecycle GUI tests |
| `Testing/run-all-gui-tests.sh` | Run all GUI tests |
| `Testing/run-isolated-gui-tests.sh` | Launch fresh instance + run tests |

## Key Test Files

| Test | What it covers |
|------|---------------|
| `YoloPollingTest` | All 3 yolo levels: timer behavior, cross-level interaction |
| `ClaudeSessionYoloTest` | Detection patterns, approval logging, state transitions |
| `ClaudeProcessHookEventTest` | Hook events, tool lifecycles, multi-agent scenarios |
| `AgentFleetProviderTest` | YAML parsing, status reading, CRUD operations |
| `AgentManagerPanelTest` | Tree rendering, context menu, provider management |
| `SessionManagerPanelTest` | Session tree, metadata persistence, categories |
| `BudgetControllerTest` | Cost tracking, threshold warnings, policy enforcement |
