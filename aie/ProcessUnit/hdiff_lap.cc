#include <adf.h>
#include <aie_api/aie.hpp>
#include <cstdint>
#include "ProcessUnit/include.h"
#include "ProcessUnit/hdiff.h"

using namespace adf;

#define kernel_load 14

void hdiff_lap(input_buffer<int32_t>& row0,
               input_buffer<int32_t>& row1,
               input_buffer<int32_t>& row2,
               input_buffer<int32_t>& row3,
               input_buffer<int32_t>& row4,
               output_buffer<int32_t>& out_flux1,
               output_buffer<int32_t>& out_flux2,
               output_buffer<int32_t>& out_flux3,
               output_buffer<int32_t>& out_flux4) {

  alignas(32) int32_t weights[8]      = {-4, -4, -4, -4, -4, -4, -4, -4};
  alignas(32) int32_t weights_rest[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

  v8int32 coeffs      = *(v8int32*)weights;
  v8int32 coeffs_rest = *(v8int32*)weights_rest;

  v8int32* ptr_out = nullptr;
  v8int32* __restrict row0_ptr = (v8int32*)row0.data();
  v8int32* __restrict row1_ptr = (v8int32*)row1.data();
  v8int32* __restrict row2_ptr = (v8int32*)row2.data();
  v8int32* __restrict row3_ptr = (v8int32*)row3.data();
  v8int32* __restrict row4_ptr = (v8int32*)row4.data();

  v16int32 data_buf1 = null_v16int32();
  v16int32 data_buf2 = null_v16int32();

  v8acc80 acc_0 = null_v8acc80();
  v8acc80 acc_1 = null_v8acc80();

  v8int32 lap_ij = null_v8int32();
  v8int32 lap_0  = null_v8int32();



  // Preload
  data_buf1 = upd_w(data_buf1, 0, *row3_ptr++);
  data_buf1 = upd_w(data_buf1, 1, *row3_ptr);
  data_buf2 = upd_w(data_buf2, 0, *row1_ptr++);
  data_buf2 = upd_w(data_buf2, 1, *row1_ptr);

  for (unsigned i = 0; i < COL / 8; i++)
    chess_prepare_for_pipelining
    chess_loop_range(1, ) {
      v16int32 flux_sub;


      acc_0 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_1 = lmul8(data_buf2, 1, 0x76543210, coeffs_rest, 0, 0x00000000);

      acc_0 = lmac8(acc_0, data_buf1, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_1 = lmac8(acc_1, data_buf1, 1, 0x76543210, coeffs_rest, 0, 0x00000000);

      row2_ptr = ((v8int32*)(row2.data())) + i;
      data_buf2 = upd_w(data_buf2, 0, *(row2_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row2_ptr));

      acc_0 = lmac8(acc_0, data_buf2, 1, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmsc8(acc_0, data_buf2, 2, 0x76543210, coeffs,      0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf2, 3, 0x76543210, coeffs_rest, 0, 0x00000000);

      lap_ij = srs(acc_0, 0);

      acc_1 = lmac8(acc_1, data_buf2, 0, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_1 = lmsc8(acc_1, data_buf2, 1, 0x76543210, coeffs,      0, 0x00000000);
      acc_1 = lmac8(acc_1, data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);

      lap_0 = srs(acc_1, 0);

      flux_sub =
          sub16(concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_0,  undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32*)out_flux1.data() + i;
      *ptr_out = ext_w(flux_sub, 0);

      acc_0 = lmul8(data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmsc8(acc_0, data_buf2, 3, 0x76543210, coeffs,      0, 0x00000000);

      row1_ptr = ((v8int32*)(row1.data())) + i;
      data_buf1 = upd_w(data_buf1, 0, *(row1_ptr)++);
      data_buf1 = upd_w(data_buf1, 1, *(row1_ptr));

      acc_0 = lmac8(acc_0, data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf2, 4, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);

      lap_0 = srs(acc_0, 0);

      flux_sub =
          sub16(concat(lap_0,  undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32*)out_flux2.data() + i;
      *ptr_out = ext_w(flux_sub, 0);

      acc_1 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);

      row0_ptr = ((v8int32*)(row0.data())) + i;
      data_buf2 = upd_w(data_buf2, 0, *(row0_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row0_ptr));

      acc_1 = lmsc8(acc_1, data_buf1, 2, 0x76543210, coeffs,      0, 0x00000000);
      acc_1 = lmac8(acc_1, data_buf1, 1, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_1 = lmac8(acc_1, data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);

      row4_ptr = ((v8int32*)(row4.data())) + i;
      data_buf2 = upd_w(data_buf2, 0, *(row4_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row4_ptr));

      acc_1 = lmac8(acc_1, data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);

      lap_0 = srs(acc_1, 0);

      flux_sub =
          sub16(concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_0,  undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32*)out_flux3.data() + i;
      *ptr_out = ext_w(flux_sub, 0);

      row3_ptr = ((v8int32*)(row3.data())) + i;
      data_buf1 = upd_w(data_buf1, 0, *(row3_ptr)++);
      data_buf1 = upd_w(data_buf1, 1, *(row3_ptr));

      acc_0 = lmsc8(acc_0, data_buf1, 2, 0x76543210, coeffs,      0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf1, 1, 0x76543210, coeffs_rest, 0, 0x00000000);

      row1_ptr = ((v8int32*)(row1.data())) + i + 1;
      data_buf2 = upd_w(data_buf2, 0, *(row1_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row1_ptr));

      acc_0 = lmac8(acc_0, data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);

      flux_sub =
          sub16(concat(srs(acc_0, 0), undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_ij,        undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32*)out_flux4.data() + i;
      *ptr_out = ext_w(flux_sub, 0);

      row3_ptr = ((v8int32*)(row3.data())) + i + 1;
      data_buf1 = upd_w(data_buf1, 0, *(row3_ptr)++);
      data_buf1 = upd_w(data_buf1, 1, *(row3_ptr));
    }

 }