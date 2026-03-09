# Budget Controls

Konsolai provides multi-level budget controls to manage AI spending.

## Per-Session Budgets

Each Claude session can have individual limits:

- **Time Limit** — Maximum session duration in minutes (0 = unlimited)
- **Cost Ceiling** — Maximum USD spend per session (0.0 = unlimited)
- **Token Ceiling** — Maximum tokens used (0 = unlimited)
- **Budget Policy** — Soft (warn) or Hard (block)
- **Warning Threshold** — Percentage at which warnings appear (default: 80%)

Configure via the Session Wizard or right-click > Edit Budget in the Session Panel.

## Global Budgets

Application-wide spending limits:

- **Weekly Budget** — Maximum USD per week across all sessions
- **Monthly Budget** — Maximum USD per month across all sessions

Configure in Konsolai Settings.

## Per-Agent Budgets

Agents defined in agent-fleet have their own budget controls:

- **Per-Run Budget** — Maximum USD per individual agent run
- **Daily Budget** — Maximum USD per day for this agent

These are defined in the agent's goal YAML file:
```yaml
max_cost: 5.0
daily_budget: 20.0
```

The Agent Panel footer shows aggregate daily spend across all agents.

## Budget Enforcement

The `BudgetController` monitors spending in real-time:

1. Token usage parsed from Claude CLI JSONL output
2. Cost calculated from token counts and model pricing
3. When threshold reached: notification + optional blocking
4. Hard policy: session blocked from sending further prompts
5. Soft policy: warning displayed, user can continue

## Token Tracking

Token usage is tracked incrementally by parsing the Claude CLI's JSONL conversation files. The `TokenUsage` struct tracks:

- Input tokens (prompt)
- Output tokens (response)
- Cache read/write tokens
- Total cost in USD
