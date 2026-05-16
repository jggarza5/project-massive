cmake_minimum_required(VERSION 3.20)

project(backtest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────
#  Dependencies via vcpkg
# ─────────────────────────────────────────────

find_package(libpqxx CONFIG REQUIRED)
find_package(DuckDB  CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

# ─────────────────────────────────────────────
#  Sources
# ─────────────────────────────────────────────

add_executable(backtest
    main.cpp
    config.cpp
    signal.cpp
    simulation.cpp
    database.cpp
    indicators.cpp
    stats.cpp
    save_results.cpp
    backtest.cpp
)

# ─────────────────────────────────────────────
#  Include path (headers in same directory)
# ─────────────────────────────────────────────

target_include_directories(backtest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ─────────────────────────────────────────────
#  Link libraries
# ─────────────────────────────────────────────

target_link_libraries(backtest PRIVATE
    libpqxx::pqxx
    duckdb
    tomlplusplus::tomlplusplus
)

# ─────────────────────────────────────────────
#  MSVC compiler flags
# ─────────────────────────────────────────────

if(MSVC)
    target_compile_options(backtest PRIVATE
        /W4           # high warning level
        /O2           # full optimization (release)
        /EHsc         # standard C++ exception handling
        /utf-8        # source and execution charset
    )
    target_compile_definitions(backtest PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX      # prevent windows.h min/max macros
    )
endif()