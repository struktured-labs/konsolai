# Agent Panel

The Agent Panel lets you monitor and steer persistent background agents from within Konsolai.

## Overview

Persistent agents run on schedules (cron-based) performing standing goals — code maintenance, content generation, monitoring, etc. The Agent Panel provides a unified view across different agent backends through a provider abstraction.

## Setup

### agent-fleet Provider

The built-in provider connects to the [agent-fleet](https://github.com/struktured/agent-fleet) framework.

1. Install agent-fleet at `~/projects/agent-fleet` (auto-detected) or configure the path:
   - Settings > Agent Fleet Path

2. Define goals in `~/projects/agent-fleet/goals/*.yaml`:
   ```yaml
   name: Fleet Ops
   project: ~/projects/agent-fleet
   goal: "Monitor fleet health, fix issues"
   schedule: "0 */4 * * *"
   model: sonnet
   max_cost: 2.0
   daily_budget: 10.0
   ```

3. The Agent Panel automatically discovers and displays all defined agents.

## Panel Layout

The Agent Panel lives in the sidebar alongside Sessions (`View > Session Manager`, then click the "Agents" tab).

Each agent shows:
- **Name** and truncated goal text
- **Status badge:** `●` running (green), `○` idle (gray), `⚠` error (red), `$` budget (orange), `⏸` paused (blue)
- **Timing:** "running 2m" or "4h ago"
- **Expanded children:** schedule, budget usage, last result, active brief + steering notes
- **Footer:** Aggregate daily spend across all agents

## Agent Actions (Right-Click)

| Action | Description |
|--------|-------------|
| Chat / Attach | Open interactive Claude session attached to the agent's tmux |
| Trigger Run... | Start an immediate run with optional task override |
| Set Brief... | Give the agent a creative direction |
| Add Steering Note... | Append a note to the current brief |
| Mark Brief Done | Complete the current brief |
| View Reports | Browse markdown reports from the agent's project |
| View Last Result | See the most recent run's output, cost, and status |
| Run History | Browse past runs with timestamps, costs, and summaries |
| Open Project | Start a new Claude session in the agent's working directory |
| Reset Session | Clear the agent's session context for a fresh start |

## Interactive Attachment

Agents aren't just headless — you can attach to an agent's tmux session to:
- Watch it work in real-time
- Ask questions about its progress
- Adjust its direction mid-run
- Debug issues interactively

## File System Layout

```
~/projects/agent-fleet/
  goals/                    # Agent definitions (YAML)
    fleet-ops.yaml
    godot-dev.yaml

~/.config/agent-fleet/
  sessions/                 # Runtime state (JSON)
    fleet-ops.json
  briefs/                   # Creative briefs (JSON)
    godot-dev.json
  budgets/                  # Daily spend tracking
    2026-03-08/
      fleet-ops.json
  results/                  # Run history
    fleet-ops/
      2026-03-08T10-00.json
```
