#pragma once
#include <stdint.h>

constexpr int GRID_ROW = 256;
constexpr int GRID_COL = 256;
constexpr int GRID_DEPTH = 1;

constexpr int ROW_BYTES = GRID_COL * sizeof(int32_t);
constexpr int FLUX_FWD_BYTES = GRID_COL * sizeof(int32_t);
constexpr int FLUX_INTER_BYTES = 2 * GRID_COL * sizeof(int32_t);