#include <adf.h>
#include <cstdint>
#include "ProcessUnit/include.h"
#include "ProcessUnit/hdiff.h"

using namespace adf;

#define kernel_load 14

alignas(32) static const int32_t weights[8]      = {-4, -4, -4, -4, -4, -4, -4, -4};
alignas(32) static const int32_t weights_rest[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

void hdiff_lap(input_buffer<int32_t>& row0,
               input_buffer<int32_t>& row1,
               input_buffer<int32_t>& row2,
               input_buffer<int32_t>& row3,
               input_buffer<int32_t>& row4,
               output_buffer<int32_t>& out_flux1,
               output_buffer<int32_t>& out_flux2,
               output_buffer<int32_t>& out_flux3,
               output_buffer<int32_t>& out_flux4) {
  v8int32 coeffs      = *(const v8int32*)weights;
  v8int32 coeffs_rest = *(const v8int32*)weights_rest;

  auto* __restrict p0 = row0.data();
  auto* __restrict p1 = row1.data();
  auto* __restrict p2 = row2.data();
  auto* __restrict p3 = row3.data();
  auto* __restrict p4 = row4.data();

  auto* __restrict o1 = out_flux1.data();
  auto* __restrict o2 = out_flux2.data();
  auto* __restrict o3 = out_flux3.data();
  auto* __restrict o4 = out_flux4.data();

  v16int32 data_buf1 = null_v16int32();
  v16int32 data_buf2 = null_v16int32();

  v8acc80 acc_0 = null_v8acc80();
  v8acc80 acc_1 = null_v8acc80();

  v8int32 lap_ij = null_v8int32();
  v8int32 lap_0  = null_v8int32();

  // preload row3 / row1
  data_buf1 = upd_w(data_buf1, 0, *(v8int32*)p3);
  p3 += 8;
  data_buf1 = upd_w(data_buf1, 1, *(v8int32*)p3);

  data_buf2 = upd_w(data_buf2, 0, *(v8int32*)p1);
  p1 += 8;
  data_buf2 = upd_w(data_buf2, 1, *(v8int32*)p1);

  for (unsigned i = 0; i < COL / 8; i++)
    chess_prepare_for_pipelining
    chess_loop_range(1, ) {
      v16int32 flux_sub;

      // lap_ij and lap_ijm
      acc_0 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_1 = lmul8(data_buf2, 1, 0x76543210, coeffs_rest, 0, 0x00000000);

      acc_0 = lmac8(acc_0, data_buf1, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_1 = lmac8(acc_1, data_buf1, 1, 0x76543210, coeffs_rest, 0, 0x00000000);

      data_buf2 = upd_w(data_buf2, 0, *(v8int32*)p2);
      p2 += 8;
      data_buf2 = upd_w(data_buf2, 1, *(v8int32*)p2);

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
      *(v8int32*)o1 = ext_w(flux_sub, 0);
      o1 += 8;

      // lap_ijp
      acc_0 = lmul8(data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmsc8(acc_0, data_buf2, 3, 0x76543210, coeffs,      0, 0x00000000);

      p1 -= 8;
      data_buf1 = upd_w(data_buf1, 0, *(v8int32*)p1);
      p1 += 8;
      data_buf1 = upd_w(data_buf1, 1, *(v8int32*)p1);

      acc_0 = lmac8(acc_0, data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf2, 4, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);

      lap_0 = srs(acc_0, 0);

      flux_sub =
          sub16(concat(lap_0,  undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      *(v8int32*)o2 = ext_w(flux_sub, 0);
      o2 += 8;

      // lap_imj and lap_ipj
      acc_1 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);

      data_buf2 = upd_w(data_buf2, 0, *(v8int32*)p0);
      p0 += 8;
      data_buf2 = upd_w(data_buf2, 1, *(v8int32*)p0);

      acc_1 = lmsc8(acc_1, data_buf1, 2, 0x76543210, coeffs,      0, 0x00000000);
      acc_1 = lmac8(acc_1, data_buf1, 1, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_1 = lmac8(acc_1, data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);

      data_buf2 = upd_w(data_buf2, 0, *(v8int32*)p4);
      p4 += 8;
      data_buf2 = upd_w(data_buf2, 1, *(v8int32*)p4);

      acc_1 = lmac8(acc_1, data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000);

      lap_0 = srs(acc_1, 0);

      flux_sub =
          sub16(concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_0,  undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      *(v8int32*)o3 = ext_w(flux_sub, 0);
      o3 += 8;

      p3 -= 8;
      data_buf1 = upd_w(data_buf1, 0, *(v8int32*)p3);
      p3 += 8;
      data_buf1 = upd_w(data_buf1, 1, *(v8int32*)p3);

      acc_0 = lmsc8(acc_0, data_buf1, 2, 0x76543210, coeffs,      0, 0x00000000);
      acc_0 = lmac8(acc_0, data_buf1, 1, 0x76543210, coeffs_rest, 0, 0x00000000);

      data_buf2 = upd_w(data_buf2, 0, *(v8int32*)p1);
      p1 += 8;
      data_buf2 = upd_w(data_buf2, 1, *(v8int32*)p1);

      acc_0 = lmac8(acc_0, data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000);

      flux_sub =
          sub16(concat(srs(acc_0, 0), undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_ij,        undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      *(v8int32*)o4 = ext_w(flux_sub, 0);
      o4 += 8;

      data_buf1 = upd_w(data_buf1, 0, *(v8int32*)p3);
      p3 += 8;
      data_buf1 = upd_w(data_buf1, 1, *(v8int32*)p3);
    }
}