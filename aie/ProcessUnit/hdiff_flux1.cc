#include <adf.h>
#include <cstdint>
#include "ProcessUnit/include.h"
#include "ProcessUnit/hdiff.h"

using namespace adf;

#define kernel_load 14

void hdiff_flux1(input_buffer<int32_t>& row1,
                 input_buffer<int32_t>& row2,
                 input_buffer<int32_t>& row3,
                 input_buffer<int32_t>& flux_forward1,
                 input_buffer<int32_t>& flux_forward2,
                 input_buffer<int32_t>& flux_forward3,
                 input_buffer<int32_t>& flux_forward4,
                 output_buffer<int32_t>& flux_inter1,
                 output_buffer<int32_t>& flux_inter2,
                 output_buffer<int32_t>& flux_inter3,
                 output_buffer<int32_t>& flux_inter4,
                 output_buffer<int32_t>& flux_inter5) {
  auto* __restrict r1 = row1.data();
  auto* __restrict r2 = row2.data();
  auto* __restrict r3 = row3.data();

  auto* __restrict f1 = flux_forward1.data();
  auto* __restrict f2 = flux_forward2.data();
  auto* __restrict f3 = flux_forward3.data();
  auto* __restrict f4 = flux_forward4.data();

  auto* __restrict o1 = flux_inter1.data();
  auto* __restrict o2 = flux_inter2.data();
  auto* __restrict o3 = flux_inter3.data();
  auto* __restrict o4 = flux_inter4.data();
  auto* __restrict o5 = flux_inter5.data();

  v16int32 data_buf1 = null_v16int32();
  v16int32 data_buf2 = null_v16int32();

  v8acc80 acc_0 = null_v8acc80();
  v8acc80 acc_1 = null_v8acc80();

  // preload
  data_buf1 = upd_w(data_buf1, 0, *(v8int32*)r1);
  r1 += 8;
  data_buf1 = upd_w(data_buf1, 1, *(v8int32*)r1);

  data_buf2 = upd_w(data_buf2, 0, *(v8int32*)r2);
  r2 += 8;
  data_buf2 = upd_w(data_buf2, 1, *(v8int32*)r2);

  for (unsigned i = 0; i < COL / 8; i++)
    chess_prepare_for_pipelining
    chess_loop_range(1, ) {
      v8int32 flux_sub;

      // flux_inter1
      flux_sub = *(v8int32*)f1;
      f1 += 8;
      acc_1 = lmul8(data_buf2, 2, 0x76543210, flux_sub, 0, 0x00000000);
      acc_1 = lmsc8(acc_1, data_buf2, 1, 0x76543210, flux_sub, 0, 0x00000000);
      *(v8int32*)o1 = flux_sub;
      o1 += 8;
      *(v8int32*)o1 = srs(acc_1, 0);
      o1 += 8;

      // flux_inter2
      flux_sub = *(v8int32*)f2;
      f2 += 8;
      acc_0 = lmul8(data_buf2, 3, 0x76543210, flux_sub, 0, 0x00000000);
      acc_0 = lmsc8(acc_0, data_buf2, 2, 0x76543210, flux_sub, 0, 0x00000000);
      *(v8int32*)o2 = flux_sub;
      o2 += 8;
      *(v8int32*)o2 = srs(acc_0, 0);
      o2 += 8;

      // flux_inter3
      flux_sub = *(v8int32*)f3;
      f3 += 8;
      acc_1 = lmul8(data_buf2, 2, 0x76543210, flux_sub, 0, 0x00000000);
      acc_1 = lmsc8(acc_1, data_buf1, 2, 0x76543210, flux_sub, 0, 0x00000000);
      *(v8int32*)o3 = flux_sub;
      o3 += 8;
      *(v8int32*)o3 = srs(acc_1, 0);
      o3 += 8;

      // row3 pair for flux_inter4
      data_buf1 = upd_w(data_buf1, 0, *(v8int32*)r3);
      r3 += 8;
      data_buf1 = upd_w(data_buf1, 1, *(v8int32*)r3);

      // flux_inter4
      flux_sub = *(v8int32*)f4;
      f4 += 8;
      acc_1 = lmul8(data_buf1, 2, 0x76543210, flux_sub, 0, 0x00000000);
      acc_1 = lmsc8(acc_1, data_buf2, 2, 0x76543210, flux_sub, 0, 0x00000000);
      *(v8int32*)o4 = flux_sub;
      o4 += 8;
      *(v8int32*)o4 = srs(acc_1, 0);
      o4 += 8;

      // reload next row1 pair
      data_buf1 = upd_w(data_buf1, 0, *(v8int32*)r1);
      r1 += 8;
      data_buf1 = upd_w(data_buf1, 1, *(v8int32*)r1);

      // flux_inter5
      *(v8int32*)o5 = ext_w(data_buf2, 1);
      o5 += 8;
      *(v8int32*)o5 = ext_w(data_buf2, 0);
      o5 += 8;

      // reload next row2 pair
      data_buf2 = upd_w(data_buf2, 0, *(v8int32*)r2);
      r2 += 8;
      data_buf2 = upd_w(data_buf2, 1, *(v8int32*)r2);
    }
}