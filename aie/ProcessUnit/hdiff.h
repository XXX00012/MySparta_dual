#pragma once
#include <adf.h>
#include <cstdint>
using namespace adf;

extern "C" {
void hdiff_lap(
    input_buffer<int32_t>& row0,
    input_buffer<int32_t>& row1,
    input_buffer<int32_t>& row2,
    input_buffer<int32_t>& row3,
    input_buffer<int32_t>& row4,
    output_buffer<int32_t>& out_flux1,
    output_buffer<int32_t>& out_flux2,
    output_buffer<int32_t>& out_flux3,
    output_buffer<int32_t>& out_flux4
);

void hdiff_flux(
    input_buffer<int32_t>& row1,
    input_buffer<int32_t>& row2,
    input_buffer<int32_t>& row3,
    input_buffer<int32_t>& flux_forward1,
    input_buffer<int32_t>& flux_forward2,
    input_buffer<int32_t>& flux_forward3,
    input_buffer<int32_t>& flux_forward4,
    output_buffer<int32_t>& out
);
}