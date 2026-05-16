-- ─────────────────────────────────────────────
--  build_bars_2025.sql
--  Builds two Parquet files from raw tick data:
--    bars_2025.parquet      — 30min bars (signal)
--    bars_2025_1min.parquet — 1min bars  (entry/exit)
--
--  Run from DuckDB CLI:
--    duckdb -c ".read build_bars_2025.sql"
-- ─────────────────────────────────────────────

PRAGMA threads=8;
PRAGMA memory_limit='4GB';

-- ─────────────────────────────────────────────
--  Step 1: Verify one day of raw data
-- ─────────────────────────────────────────────

SELECT
    ticker,
    count(*)                        AS tick_count,
    min(participant_timestamp)      AS first_tick,
    max(participant_timestamp)      AS last_tick
FROM read_csv(
    'C:\code\python\forex-data\quotes_v1\2025\01\2025-01-01.csv.gz',
    header  = true,
    columns = {
        'ticker':                'VARCHAR',
        'ask_exchange':          'INTEGER',
        'ask_price':             'DOUBLE',
        'bid_exchange':          'INTEGER',
        'bid_price':             'DOUBLE',
        'participant_timestamp': 'BIGINT'
    }
)
WHERE ticker IN (
    'C:EUR-USD','C:GBP-USD','C:USD-JPY','C:USD-CHF',
    'C:AUD-USD','C:USD-CAD','C:NZD-USD',
    'C:EUR-JPY','C:GBP-JPY','C:AUD-JPY'
)
GROUP BY ticker
ORDER BY ticker;

-- ─────────────────────────────────────────────
--  Step 2: Build normalized raw ticks
--  Read all of 2025 in one glob pass.
--  Store in memory for reuse across both
--  30min and 1min aggregations.
-- ─────────────────────────────────────────────

CREATE OR REPLACE TABLE raw_ticks_2025 AS
SELECT
    regexp_replace(
        replace(ticker, 'C:', ''),
        '-', '', 'g'
    )                                           AS symbol,
    to_timestamp(participant_timestamp / 1e9)   AS ts,
    bid_price,
    ask_price
FROM read_csv(
    'C:\code\python\forex-data\quotes_v1\2025\**\*.csv.gz',
    header        = true,
    columns       = {
        'ticker':                'VARCHAR',
        'ask_exchange':          'INTEGER',
        'ask_price':             'DOUBLE',
        'bid_exchange':          'INTEGER',
        'bid_price':             'DOUBLE',
        'participant_timestamp': 'BIGINT'
    },
    union_by_name = true
)
WHERE ticker IN (
    'C:EUR-USD','C:GBP-USD','C:USD-JPY','C:USD-CHF',
    'C:AUD-USD','C:USD-CAD','C:NZD-USD',
    'C:EUR-JPY','C:GBP-JPY','C:AUD-JPY'
)
AND bid_price > 0
AND ask_price > 0
AND ask_price >= bid_price;

SELECT 'Raw ticks loaded: ' || count(*) || ' rows' AS status
FROM raw_ticks_2025;

-- ─────────────────────────────────────────────
--  Step 3: Build 30min bars
-- ─────────────────────────────────────────────

CREATE OR REPLACE TABLE bars_30min_2025 AS
SELECT
    symbol,
    time_bucket(INTERVAL '30 minutes', ts) AS bar_time,
    first(bid_price ORDER BY ts)           AS bid_open,
    max(bid_price)                         AS bid_high,
    min(bid_price)                         AS bid_low,
    last(bid_price ORDER BY ts)            AS bid_close,
    first(ask_price ORDER BY ts)           AS ask_open,
    max(ask_price)                         AS ask_high,
    min(ask_price)                         AS ask_low,
    last(ask_price ORDER BY ts)            AS ask_close,
    count(*)                               AS tick_count
FROM raw_ticks_2025
GROUP BY symbol, bar_time
ORDER BY symbol, bar_time;

-- Sanity check
SELECT
    symbol,
    count(*)        AS bar_count,
    min(bar_time)   AS first_bar,
    max(bar_time)   AS last_bar,
    avg(tick_count) AS avg_ticks,
    sum(CASE WHEN bid_close > ask_close THEN 1 ELSE 0 END) AS crossed_bars
FROM bars_30min_2025
GROUP BY symbol
ORDER BY symbol;

-- ─────────────────────────────────────────────
--  Step 4: Build 1min bars
-- ─────────────────────────────────────────────

CREATE OR REPLACE TABLE bars_1min_2025 AS
SELECT
    symbol,
    time_bucket(INTERVAL '1 minute', ts)   AS bar_time,
    first(bid_price ORDER BY ts)           AS bid_open,
    max(bid_price)                         AS bid_high,
    min(bid_price)                         AS bid_low,
    last(bid_price ORDER BY ts)            AS bid_close,
    first(ask_price ORDER BY ts)           AS ask_open,
    max(ask_price)                         AS ask_high,
    min(ask_price)                         AS ask_low,
    last(ask_price ORDER BY ts)            AS ask_close,
    count(*)                               AS tick_count
FROM raw_ticks_2025
GROUP BY symbol, bar_time
ORDER BY symbol, bar_time;

-- Sanity check
SELECT
    symbol,
    count(*)        AS bar_count,
    min(bar_time)   AS first_bar,
    max(bar_time)   AS last_bar,
    avg(tick_count) AS avg_ticks,
    sum(CASE WHEN bid_close > ask_close THEN 1 ELSE 0 END) AS crossed_bars
FROM bars_1min_2025
GROUP BY symbol
ORDER BY symbol;

-- Thin bar check on weekdays
SELECT symbol, bar_time, tick_count
FROM bars_1min_2025
WHERE tick_count < 2
  AND EXTRACT(dow FROM bar_time) BETWEEN 1 AND 5
ORDER BY tick_count
LIMIT 20;

-- ─────────────────────────────────────────────
--  Step 5: Export both Parquet files
-- ─────────────────────────────────────────────

COPY bars_30min_2025 TO 'C:\code\python\forex-data\bars_2025.parquet'
(FORMAT PARQUET, COMPRESSION 'zstd', ROW_GROUP_SIZE 100000);

SELECT '30min Parquet written' AS status;

COPY bars_1min_2025 TO 'C:\code\python\forex-data\bars_2025_1min.parquet'
(FORMAT PARQUET, COMPRESSION 'zstd', ROW_GROUP_SIZE 100000);

SELECT '1min Parquet written' AS status;

-- ─────────────────────────────────────────────
--  Step 6: Verify both files
-- ─────────────────────────────────────────────

SELECT '30min' AS resolution, symbol, count(*) AS bars
FROM read_parquet('C:\code\python\forex-data\bars_2025.parquet')
GROUP BY symbol ORDER BY symbol;

SELECT '1min' AS resolution, symbol, count(*) AS bars
FROM read_parquet('C:\code\python\forex-data\bars_2025_1min.parquet')
GROUP BY symbol ORDER BY symbol;

-- ─────────────────────────────────────────────
--  Step 7: Cleanup
-- ─────────────────────────────────────────────

DROP TABLE raw_ticks_2025;
DROP TABLE bars_30min_2025;
DROP TABLE bars_1min_2025;

SELECT 'Done.' AS status;
