---
name: feedback-sweep-no-db
description: When working on Backtest::sweep in backtest.cpp, do not write results to Postgres — sweep should only print the top-10 leaderboard to the terminal.
metadata:
  type: feedback
---

`Backtest::sweep` must NOT touch the database. No `ResultWriter`, no
`create_tables`, no `writer.save`. Each `BacktestRun` should carry the label
in the `run_id` slot. `main.cpp` calls `print_leaderboard(runs, 10)` for the
top-10 terminal output.

**Why:** The user has asked for this fix three separate times after refactors
re-introduced DB writes in sweep. Sweeps generate hundreds-to-thousands of
combinations and writing each to Postgres is unwanted noise; only `run` and
`wfo` should persist. They explicitly want sweep results displayed in the
terminal only.

**How to apply:** Any time you (re)build `Backtest::sweep` after a refactor
that touched [[project-code-structure]], check that the function does not
construct a `ResultWriter`, and that the runs vector stores the label in the
`run_id` field rather than calling `writer.save`. The single `run` and the
`wfo` paths should still write to the DB — only `sweep` is terminal-only.
