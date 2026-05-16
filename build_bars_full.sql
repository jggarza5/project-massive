-- ─────────────────────────────────────────────
--  build_bars_full.sql
--  Builds two Parquet files covering 2009-2026:
--    daily_2009_2026.parquet     — from day_aggs_v1
--    sub_30min_2009_2026.parquet — from minute_aggs_v1
--
--  Sub-bar resolution is set by the INTERVAL
--  in Step 3. Change '30 minutes' to '15 minutes',
--  '1 hour' etc. and rebuild if needed.
--
--  Runtime: 30-60 minutes on a typical machine.
--
--  Run:
--    duckdb -c ".read build_bars_full.sql"
-- ─────────────────────────────────────────────

PRAGMA threads        = 8;
PRAGMA memory_limit   = '8GB';

-- ─────────────────────────────────────────────
--  Shared ticker filter
-- ─────────────────────────────────────────────

CREATE OR REPLACE MACRO our_tickers(t) AS
    t IN (
        'C:EUR-USD','C:GBP-USD','C:USD-JPY','C:USD-CHF',
        'C:AUD-USD','C:USD-CAD','C:NZD-USD',
        'C:EUR-JPY','C:GBP-JPY','C:AUD-JPY'
    );

-- ─────────────────────────────────────────────
--  Shared ticker normalizer
--    C:EUR-USD → EURUSD
-- ─────────────────────────────────────────────

CREATE OR REPLACE MACRO normalize(t) AS
    regexp_replace(replace(t, 'C:', ''), '-', '', 'g');

-- ─────────────────────────────────────────────
--  Step 1: Verify daily data sample
-- ─────────────────────────────────────────────

SELECT
    normalize(ticker)             AS symbol,
    count(*)                      AS day_count,
    min(to_timestamp(window_start / 1e9)) AS first_day,
    max(to_timestamp(window_start / 1e9)) AS last_day
FROM read_csv(
    'C:\code\python\forex-data\day_aggs_v1\2009\09\2009-09-25.csv.gz',
    header  = true,
    columns = {
        'ticker':       'VARCHAR',
        'volume':       'DOUBLE',
        'open':         'DOUBLE',
        'close':        'DOUBLE',
        'high':         'DOUBLE',
        'low':          'DOUBLE',
        'window_start': 'BIGINT',
        'transactions': 'BIGINT'
    }
)
WHERE our_tickers(ticker)
GROUP BY normalize(ticker)
ORDER BY symbol;

-- ─────────────────────────────────────────────
--  Step 2: Build daily bars (2009-2026)
--  Source: day_aggs_v1
--
--  Since there is no bid/ask in aggregate data,
--  open/high/low/close are mid prices.
--  All bid_ and ask_ fields are set to mid.
--  Spread is applied at execution time in C++.
-- ─────────────────────────────────────────────

SELECT 'Building daily bars...' AS status;

COPY (
    SELECT
        normalize(ticker)                       AS symbol,
        to_timestamp(window_start / 1e9)        AS bar_time,

        -- Mid price → stored in both bid and ask columns
        -- C++ applies spread at entry execution
        open    AS bid_open,
        high    AS bid_high,
        low     AS bid_low,
        close   AS bid_close,

        open    AS ask_open,
        high    AS ask_high,
        low     AS ask_low,
        close   AS ask_close,

        CAST(volume AS INTEGER)                 AS tick_count

    FROM read_csv(
        'C:\code\python\forex-data\day_aggs_v1\**\*.csv.gz',
        header        = true,
        columns       = {
            'ticker':       'VARCHAR',
            'volume':       'DOUBLE',
            'open':         'DOUBLE',
            'close':        'DOUBLE',
            'high':         'DOUBLE',
            'low':          'DOUBLE',
            'window_start': 'BIGINT',
            'transactions': 'BIGINT'
        },
        union_by_name = true
    )
    WHERE our_tickers(ticker)
      AND open  > 0
      AND close > 0
      AND high  >= low
    ORDER BY symbol, bar_time
)
TO 'C:\code\python\forex-data\daily_2009_2026.parquet'
(FORMAT PARQUET, COMPRESSION 'zstd', ROW_GROUP_SIZE 50000);

SELECT 'Daily bars written.' AS status;

-- Verify
SELECT symbol, count(*) AS days, min(bar_time) AS first, max(bar_time) AS last
FROM read_parquet('C:\code\python\forex-data\daily_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

-- ─────────────────────────────────────────────
--  Step 3: Build sub-bar bars (2009-2026)
--  Source: minute_aggs_v1
--
--  Resolution: 30 minutes (change INTERVAL below
--  to use a different sub-bar timeframe).
--
--  Aggregates 1-min bars → 30-min OHLCV bars.
--  Same mid-price convention as daily bars.
-- ─────────────────────────────────────────────

SELECT 'Building sub-bar (30-min) bars — this will take a while...' AS status;

COPY (
    WITH raw AS (
        SELECT
            normalize(ticker)                   AS symbol,
            to_timestamp(window_start / 1e9)    AS ts,
            open, high, low, close,
            volume
        FROM read_csv(
            'C:\code\python\forex-data\minute_aggs_v1\**\*.csv.gz',
            header        = true,
            columns       = {
                'ticker':       'VARCHAR',
                'volume':       'DOUBLE',
                'open':         'DOUBLE',
                'close':        'DOUBLE',
                'high':         'DOUBLE',
                'low':          'DOUBLE',
                'window_start': 'BIGINT',
                'transactions': 'BIGINT'
            },
            union_by_name = true
        )
        WHERE our_tickers(ticker)
          AND open  > 0
          AND close > 0
          AND high  >= low
    )
    SELECT
        symbol,
        -- ↓ Change interval here to build different resolutions
        time_bucket(INTERVAL '30 minutes', ts)  AS bar_time,

        first(open  ORDER BY ts)                AS bid_open,
        max(high)                               AS bid_high,
        min(low)                                AS bid_low,
        last(close  ORDER BY ts)                AS bid_close,

        first(open  ORDER BY ts)                AS ask_open,
        max(high)                               AS ask_high,
        min(low)                                AS ask_low,
        last(close  ORDER BY ts)                AS ask_close,

        count(*)::INTEGER                       AS tick_count

    FROM raw
    GROUP BY symbol, bar_time
    ORDER BY symbol, bar_time
)
TO 'C:\code\python\forex-data\sub_30min_2009_2026.parquet'
(FORMAT PARQUET, COMPRESSION 'zstd', ROW_GROUP_SIZE 100000);

SELECT 'Sub-bar Parquet written.' AS status;

-- Verify
SELECT symbol, count(*) AS bars, min(bar_time) AS first, max(bar_time) AS last
FROM read_parquet('C:\code\python\forex-data\sub_30min_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

-- ─────────────────────────────────────────────
--  Step 4: Data quality checks
-- ─────────────────────────────────────────────

-- Daily: crossed bars (high < low)
SELECT 'Daily crossed bars:' AS check,
    count(*) AS count
FROM read_parquet('C:\code\python\forex-data\daily_2009_2026.parquet')
WHERE bid_high < bid_low;

-- Sub-bar: crossed bars
SELECT 'Sub-bar crossed bars:' AS check,
    count(*) AS count
FROM read_parquet('C:\code\python\forex-data\sub_30min_2009_2026.parquet')
WHERE bid_high < bid_low;

-- Daily bar count per symbol (should all be similar ~4200 trading days)
SELECT symbol, count(*) AS days
FROM read_parquet('C:\code\python\forex-data\daily_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

SELECT 'Done.' AS status;
