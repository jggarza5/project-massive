-- ─────────────────────────────────────────────
--  build_1min.sql
--  Builds a 1-minute execution bar Parquet file
--  from minute_aggs_v1 (2009-2026).
--
--  No aggregation needed — data is already at
--  1-minute resolution. Just reads CSVs, filters
--  to our 10 pairs, and writes to Parquet.
--
--  Signal bar:  daily_2009_2026.parquet (unchanged)
--  Execution:   sub_1min_2009_2026.parquet (new)
--
--  Expected size: ~2-4 GB compressed
--  Runtime:       20-40 minutes
--
--  Run:
--    duckdb -c ".read build_1min.sql"
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
--  Verify sample file
-- ─────────────────────────────────────────────

SELECT 'Verifying sample file...' AS status;

SELECT
    normalize(ticker) AS symbol,
    count(*)          AS rows,
    min(to_timestamp(window_start / 1e9)) AS first_bar,
    max(to_timestamp(window_start / 1e9)) AS last_bar
FROM read_csv(
    'C:\code\python\forex-data\minute_aggs_v1\2009\10\2009-10-09.csv.gz',
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
--  Build 1-minute Parquet (2009-2026)
--  No time_bucket aggregation needed —
--  data is already 1-minute resolution.
--  window_start nanoseconds → epoch seconds.
-- ─────────────────────────────────────────────

SELECT 'Building 1-minute bars — this will take a while...' AS status;

COPY (
    SELECT
        normalize(ticker)                       AS symbol,
        to_timestamp(window_start / 1e9)        AS bar_time,

        -- Mid price stored in both bid and ask fields.
        -- Spread applied at execution time in C++.
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
    ORDER BY symbol, bar_time
)
TO 'C:\code\python\forex-data\sub_1min_2009_2026.parquet'
(FORMAT PARQUET, COMPRESSION 'zstd', ROW_GROUP_SIZE 200000);

SELECT '1-minute bars written.' AS status;

-- ─────────────────────────────────────────────
--  Verify output
-- ─────────────────────────────────────────────

SELECT
    symbol,
    count(*)        AS bars,
    min(bar_time)   AS first,
    max(bar_time)   AS last
FROM read_parquet('C:\code\python\forex-data\sub_1min_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

-- Quality check
SELECT '1-min crossed bars:' AS check,
    count(*) AS count
FROM read_parquet('C:\code\python\forex-data\sub_1min_2009_2026.parquet')
WHERE bid_high < bid_low;

-- Bars per day sanity check (expect ~390-420 per trading day)
SELECT
    symbol,
    count(*) / count(DISTINCT bar_time::DATE) AS avg_bars_per_day
FROM read_parquet('C:\code\python\forex-data\sub_1min_2009_2026.parquet')
GROUP BY symbol ORDER BY symbol;

SELECT 'Done.' AS status;