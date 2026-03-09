# Implementing Agent Providers

This guide explains how to implement a custom `AgentProvider` for Konsolai.

## Overview

The `AgentProvider` abstract class defines the interface between Konsolai's Agent Panel and any agent management backend. Providers are loaded at startup and registered with `AgentManagerPanel::addProvider()`.

## Interface Version

Providers declare their interface version via `interfaceVersion()`. This enables forward-compatible evolution:

- **v1** (current): All methods in `AgentProvider.h` are required except `pauseSchedule()` and `resumeSchedule()` which have default no-op implementations.
- **v2+** (future): New optional methods will be added with default implementations. The panel checks `provider->interfaceVersion() >= N` before calling v2+ methods.

## Required Methods

```cpp
class MyProvider : public Konsolai::AgentProvider
{
    Q_OBJECT

public:
    int interfaceVersion() const override { return 1; }
    QString name() const override { return QStringLiteral("my-provider"); }
    bool isAvailable() const override;

    // Agent listing and status
    QList<AgentInfo> agents() const override;
    AgentStatus agentStatus(const QString &id) const override;

    // Actions
    bool triggerRun(const QString &id, const QString &task) override;
    bool setBrief(const QString &id, const QString &direction) override;
    bool addSteeringNote(const QString &id, const QString &note) override;
    bool markBriefDone(const QString &id) override;

    // Read operations
    QList<AgentReport> recentReports(const QString &id, int count) const override;
    QList<AgentRunResult> recentResults(const QString &id, int count) const override;
    AgentRunResult lastResult(const QString &id) const override;
    AgentAttachInfo attachInfo(const QString &id) const override;

    // CRUD
    bool createAgent(const AgentConfig &config) override;
    bool updateAgent(const QString &id, const AgentConfig &config) override;
    bool deleteAgent(const QString &id) override;
    bool resetSession(const QString &id) override;
};
```

## Data Types

All data types are defined in `AgentProvider.h`:

- **AgentInfo** — Static agent definition (name, project, schedule, goal, budget)
- **AgentStatus** — Runtime state (idle/running/error/budget/paused, last run, daily spend)
- **AgentBrief** — Creative direction with steering notes
- **AgentReport** — Markdown report with timestamp and file path
- **AgentRunResult** — Run outcome (status, summary, cost, exit code, transcript path)
- **AgentAttachInfo** — How to attach interactively (tmux session, SSH info)
- **AgentConfig** — Full agent configuration for create/update
- **AgentBudget** — Per-run and daily cost limits with model selection

## Signals

Emit these signals to trigger UI updates:

```cpp
Q_EMIT agentChanged(agentId);   // Status of one agent changed
Q_EMIT agentsReloaded();        // Agent list changed (added/removed)
```

## Registration

In `MainWindow.cpp`, create and register your provider:

```cpp
auto *myProvider = new MyProvider();
if (myProvider->isAvailable()) {
    _agentPanel->addProvider(myProvider);
} else {
    delete myProvider;
}
```

## Testing

Follow the pattern in `AgentFleetProviderTest.cpp`:

1. Use `QTemporaryDir` for filesystem isolation
2. Test `isAvailable()` with and without required files
3. Test parsing with mock data files
4. Test CRUD operations
5. Verify signal emission with `QSignalSpy`

Panel tests (`AgentManagerPanelTest.cpp`) verify tree rendering and context menu integration.
