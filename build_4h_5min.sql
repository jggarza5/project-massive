-- ─────────────────────────────────────────────
--  build_4h_5min.sql
--  Builds two Parquet files from minute_aggs_v1:
--    4hour_2009_2026.parquet  — signal bars
--    sub_5min_2009_2026.parquet — execution bars
--
--  Same 48:1 ratio as the daily/30min setup:
--    daily÷30min  = 1440÷30 = 48
--    4hour÷5min   =  240÷5  = 48
--
--  Run:
--    duckdb -c ".read build_4h_5min.sql"
-- ─────────────────────────────────────────────

PRAGMA threads      = 8;
PRAGMA memory_limit = '8GB';

CREATE OR REPLACE MACRO our_tickers(t) AS
    t IN (
        'C:EUR-USD','C:GBP-USD','C:USD-JPY','C:USD-CHF',
        'C:AUD-USD','C:USD-CAD','C:NZD-USD',
        'C:EUR-JPY','C:GBP-JPY','C:AUD-JPY'
    );

CREATE OR REPLACE MACRO normalize(t) AS
    regexp_replace(replace(t, 'C:', ''), '-', '', 'g');

-- ─────────────────────────────────────────────
--  Shared raw CTE source
--  Read once, used for both outputs via
--  separate COPY statements.
-- ─────────────────────────────────────────────

-- Step 1: 4-hour signal bars
-- ─────────────────────────────────────────────

SELECT 'Building 4-hour signal bars...' AS status;

COPY (
    WITH raw AS (
        SELECT
            normalize(ticker)                   AS symbol,
            to_timestamp(window_start / 1e9)    AS ts,
            open, high, low, close, volume
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
        time_bucket(INTERVAL '4 hours', ts)     AS bar_time,

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
TO 'C:\code\python\forex-data\4hour_2009_2026.parquet'
(FORMAT PARQUET, COMPRESSION 'zstd', ROW_GROUP_SIZE 50000);

SELECT '4-hour bars written.' AS status;

-- Verify
SELECT symbol, count(*) AS bars,
       min(bar_time) AS first, max(bar_time) AS last
FROM read_parquet('C:\code\python\forex-data\4hour_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

-- ─────────────────────────────────────────────
--  Step 2: 5-minute execution bars
-- ─────────────────────────────────────────────

SELECT 'Building 5-minute execution bars — this will take a while...' AS status;

COPY (
    WITH raw AS (
        SELECT
            normalize(ticker)                   AS symbol,
            to_timestamp(window_start / 1e9)    AS ts,
            open, high, low, close, volume
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
        time_bucket(INTERVAL '5 minutes', ts)   AS bar_time,

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
TO 'C:\code\python\forex-data\sub_5min_2009_2026.parquet'
(FORMAT PARQUET, COMPRESSION 'zstd', ROW_GROUP_SIZE 200000);

SELECT '5-minute bars written.' AS status;

-- Verify
SELECT symbol, count(*) AS bars,
       min(bar_time) AS first, max(bar_time) AS last
FROM read_parquet('C:\code\python\forex-data\sub_5min_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

-- ─────────────────────────────────────────────
--  Step 3: Quality checks
-- ─────────────────────────────────────────────

SELECT '4-hour crossed bars:' AS check,
    count(*) AS count
FROM read_parquet('C:\code\python\forex-data\4hour_2009_2026.parquet')
WHERE bid_high < bid_low;

SELECT '5-min crossed bars:' AS check,
    count(*) AS count
FROM read_parquet('C:\code\python\forex-data\sub_5min_2009_2026.parquet')
WHERE bid_high < bid_low;

-- Bars per day sanity check on 4-hour data (expect ~6)
SELECT symbol,
       count(*) / count(DISTINCT bar_time::DATE) AS avg_bars_per_day
FROM read_parquet('C:\code\python\forex-data\4hour_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

SELECT 'Done.' AS status;