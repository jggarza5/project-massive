# Backtest Viewer — Project Spec

## Overview
A web application that connects to an existing PostgreSQL database and lets the
user browse, compare, and analyze forex backtest results produced by a C++
backtesting engine. All data already exists in the database — the app is
read-only.

---

## Tech Stack

| Layer     | Choice            | Reason                                      |
|-----------|-------------------|---------------------------------------------|
| Backend   | FastAPI (Python)  | Async, auto-docs, easy PostgreSQL via asyncpg |
| Frontend  | React + Recharts  | Interactive charts, clean component model   |
| DB driver | asyncpg + SQLAlchemy (async) | Non-blocking queries             |
| Styling   | Tailwind CSS      | Utility-first, no design system needed      |

> Alternative: If you want the fastest possible delivery, use **Streamlit**
> (pure Python, single file). The FastAPI + React version is more polished.

---

## Database Schema (read-only, already exists)

```sql
-- Summary of each backtest run
bt_runs (
    run_id                  TEXT PRIMARY KEY,   -- e.g. "20250101_143022_label"
    label                   TEXT,
    created_at              TIMESTAMPTZ,
    entry_donchian_period   INT,
    atr_period              INT,
    trigger_atr_mult        DOUBLE PRECISION,
    tp_atr_mult             DOUBLE PRECISION,
    sl_atr_mult             DOUBLE PRECISION,
    lots                    DOUBLE PRECISION,
    initial_equity          DOUBLE PRECISION,
    final_equity            DOUBLE PRECISION,
    net_pnl                 DOUBLE PRECISION,
    total_trades            INT,
    win_rate                DOUBLE PRECISION,
    profit_factor           DOUBLE PRECISION,
    max_drawdown            DOUBLE PRECISION,
    sharpe_ratio            DOUBLE PRECISION,
    sortino_ratio           DOUBLE PRECISION
)

-- Individual trades
bt_trades (
    id              BIGSERIAL PRIMARY KEY,
    run_id          TEXT,
    symbol          TEXT,           -- e.g. "EURUSD"
    direction       TEXT,           -- "Long" or "Short"
    mode            TEXT,           -- "Mean-rev" or "Trend"
    lots            DOUBLE PRECISION,
    entry_time      TIMESTAMPTZ,
    exit_time       TIMESTAMPTZ,
    entry_price     DOUBLE PRECISION,
    exit_price      DOUBLE PRECISION,
    exit_reason     TEXT,           -- "TakeProfit", "StopLoss", "EndOfData"
    raw_pnl         DOUBLE PRECISION,
    pnl_usd         DOUBLE PRECISION
)

-- Bar-by-bar equity curve
bt_equity (
    run_id          TEXT,
    bar_time        TIMESTAMPTZ,
    equity          DOUBLE PRECISION,
    open_positions  INT,
    PRIMARY KEY (run_id, bar_time)
)

-- Key-value metrics
bt_stats (
    run_id  TEXT,
    metric  TEXT,
    value   DOUBLE PRECISION,
    PRIMARY KEY (run_id, metric)
)

-- Per-symbol breakdown
bt_symbol_stats (
    run_id   TEXT,
    symbol   TEXT,
    trades   INT,
    net_pnl  DOUBLE PRECISION,
    win_rate DOUBLE PRECISION,
    PRIMARY KEY (run_id, symbol)
)
```

---

## Features

### Page 1 — Run List
- Table of all backtest runs sorted by `created_at` descending
- Columns: Label, Date, Entry Period, Trig, TP, SL, Trades, Win Rate, P&L, Sharpe
- Click any row → navigate to Run Detail page
- Search/filter by label
- Sort by any column

### Page 2 — Run Detail
Accessible via `/run/{run_id}`

#### Section A — Summary Cards (top row)
Small stat cards showing:
- Net P&L (USD)
- Total Trades
- Win Rate %
- Profit Factor
- Max Drawdown
- Sharpe Ratio
- Sortino Ratio

#### Section B — Equity Curve
- Line chart of equity over time (from `bt_equity`)
- X-axis: date, Y-axis: USD equity
- Show `open_positions` as a secondary shaded area (optional)
- Zoom and pan support

#### Section C — Parameters
Small table showing the exact parameters used:
- strategy_mode, entry_donchian_period, atr_period
- trigger_atr_mult, tp_atr_mult, sl_atr_mult, lots

#### Section D — Per-Symbol Stats
Table from `bt_symbol_stats`:
- Symbol | Trades | Net P&L | Win Rate
- Sortable, colored P&L (green/red)

#### Section E — Trade List
Paginated table from `bt_trades`:
- Columns: Symbol, Direction, Mode, Entry Time, Exit Time, Entry Price,
  Exit Price, Exit Reason, P&L
- Filter by: Symbol, Direction, Mode, Exit Reason
- Sort by any column
- Color rows: green (profit), red (loss)
- Pagination: 50 trades per page

#### Section F — Charts
- **P&L Distribution**: histogram of trade P&L values
- **P&L by Symbol**: bar chart of net P&L per symbol
- **Win Rate by Symbol**: bar chart
- **Exit Reason Breakdown**: pie chart (TP vs SL vs EndOfData)
- **P&L by Direction**: Long vs Short comparison
- **P&L by Mode**: Mean-rev vs Trend comparison (if Both mode was used)
- **Monthly P&L**: bar chart of P&L aggregated by month

---

## API Endpoints (FastAPI)

```
GET /api/runs
    Returns list of all runs with summary fields
    Query params: limit, offset, sort_by, sort_dir, search

GET /api/runs/{run_id}
    Returns full run metadata + all bt_stats metrics

GET /api/runs/{run_id}/equity
    Returns equity curve data (downsampled to max 2000 points if needed)

GET /api/runs/{run_id}/trades
    Returns paginated trade list
    Query params: page, page_size, symbol, direction, mode, exit_reason, sort_by

GET /api/runs/{run_id}/symbol-stats
    Returns per-symbol breakdown

GET /api/runs/{run_id}/charts
    Returns pre-aggregated data for all charts in one call:
    - pnl_distribution (histogram bins)
    - pnl_by_symbol
    - win_rate_by_symbol
    - exit_reasons (counts)
    - monthly_pnl
    - pnl_by_direction
    - pnl_by_mode
```

---

## Project Structure

```
backtest-viewer/
├── backend/
│   ├── main.py           # FastAPI app
│   ├── database.py       # asyncpg connection pool
│   ├── models.py         # Pydantic response models
│   ├── routers/
│   │   ├── runs.py
│   │   ├── trades.py
│   │   ├── equity.py
│   │   └── charts.py
│   └── requirements.txt
├── frontend/
│   ├── src/
│   │   ├── App.jsx
│   │   ├── pages/
│   │   │   ├── RunList.jsx
│   │   │   └── RunDetail.jsx
│   │   └── components/
│   │       ├── SummaryCards.jsx
│   │       ├── EquityChart.jsx
│   │       ├── TradeTable.jsx
│   │       ├── SymbolStats.jsx
│   │       └── ChartPanel.jsx
│   ├── package.json
│   └── tailwind.config.js
└── README.md
```

---

## Database Connection

The PostgreSQL database is on localhost. Connection config:

```python
DATABASE_URL = "postgresql://postgres@localhost:5432/massive"
```

Expose as an environment variable. Use asyncpg connection pool with max 5 connections.

---

## Instructions for Claude Code

1. Scaffold the FastAPI backend first — all endpoints returning mock data
2. Verify database connection and test queries against real tables
3. Build the React frontend, starting with RunList page
4. Build RunDetail page section by section (cards → equity chart → trade table → charts)
5. Wire frontend to backend API
6. Add loading states and error handling throughout
7. Run with: `uvicorn main:app --reload` (backend) and `npm run dev` (frontend)

The C++ backtester writes to PostgreSQL after every run. The web app is
read-only and never writes to the database.