//===- hdiff_lap.cc ---------------------------------------------*- C++ -*-===//
//
// (c) 2023 SAFARI Research Group at ETH Zurich, Gagandeep Singh, D-ITET
//
// This file is licensed under the MIT License.
// SPDX-License-Identifier: MIT
// 
//
//===----------------------------------------------------------------------===//

#include <adf.h>
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

  alignas(32) int32_t weights[8] = {-4, -4, -4, -4, -4, -4, -4, -4};

  alignas(32) int32_t weights_rest[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  

  v8int32 coeffs = *(v8int32 *)weights;           //  8 x int32 = 256b W vector

  v8int32 coeffs_rest = *(v8int32 *)weights_rest; //  8 x int32 = 256b W vector


  v8int32 *ptr_out = (v8int32 *)out_flux1.data();
  v8int32* __restrict row0_ptr = (v8int32 *)row0.data();
  v8int32* __restrict row1_ptr = (v8int32 *)row1.data();
  v8int32* __restrict row2_ptr = (v8int32 *)row2.data();
  v8int32* __restrict row3_ptr = (v8int32 *)row3.data();
  v8int32* __restrict row4_ptr = (v8int32 *)row4.data();


  v16int32 data_buf1 = null_v16int32();
  v16int32 data_buf2 = null_v16int32();

  v8acc80 acc_0 = null_v8acc80();
  v8acc80 acc_1 = null_v8acc80();

  v8int32 lap_ij = null_v8int32(); //  8 x int32 = 256b W vector
  v8int32 lap_0 = null_v8int32();  //  8 x int32 = 256b W vector

  data_buf1 = upd_w(data_buf1, 0, *row3_ptr++);
  data_buf1 = upd_w(data_buf1, 1, *row3_ptr);
  data_buf2 = upd_w(data_buf2, 0, *row1_ptr++);
  data_buf2 = upd_w(data_buf2, 1, *row1_ptr);

  for (unsigned i = 0; i < COL / 8; i++)
    chess_prepare_for_pipelining chess_loop_range(1, ) {
      v16int32 flux_sub;

      acc_0 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000); // c
      acc_1 = lmul8(data_buf2, 1, 0x76543210, coeffs_rest, 0, 0x00000000); // b

      acc_0 = lmac8(acc_0, data_buf1, 2, 0x76543210, coeffs_rest, 0,
                    0x00000000); // c,k
      acc_1 = lmac8(acc_1, data_buf1, 1, 0x76543210, coeffs_rest, 0,
                    0x00000000); // b,j

      row2_ptr = ((v8int32 *)(row2.data())) + i;
      data_buf2 = upd_w(data_buf2, 0, *(row2_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row2_ptr));

      acc_0 = lmac8(acc_0, data_buf2, 1, 0x76543210, coeffs_rest, 0,
                    0x00000000); // c,k,f
      acc_0 = lmsc8(acc_0, data_buf2, 2, 0x76543210, coeffs, 0,
                    0x00000000); // c,k,f,4*g
      acc_0 = lmac8(acc_0, data_buf2, 3, 0x76543210, coeffs_rest, 0,
                    0x00000000); // c,k,f,4*g,h

      lap_ij = srs(acc_0, 0); // store lap_ij

      acc_1 = lmac8(acc_1, data_buf2, 0, 0x76543210, coeffs_rest, 0,
                    0x00000000); // b,j,e
      acc_1 = lmsc8(acc_1, data_buf2, 1, 0x76543210, coeffs, 0,
                    0x00000000); // b,j,e,4*f
      acc_1 = lmac8(acc_1, data_buf2, 2, 0x76543210, coeffs_rest, 0,
                    0x00000000); // b,j,e,4*f,g

      // lap_ijm
      lap_0 = srs(acc_1, 0);

      // Calculate  lap_ij - lap_ijm

      flux_sub =
          sub16(concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_0, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32 *)out_flux1.data() + i;
      *ptr_out = ext_w(flux_sub, 0);
    

      acc_0 = lmul8(data_buf1, 3, 0x76543210, coeffs_rest, 0, 0x00000000); // l
      acc_0 = lmsc8(acc_0, data_buf2, 3, 0x76543210, coeffs, 0,
                    0x00000000); // l, 4*h

      row1_ptr = ((v8int32 *)(row1.data())) + i;
      data_buf1 = upd_w(data_buf1, 0, *(row1_ptr)++);
      data_buf1 = upd_w(data_buf1, 1, *(row1_ptr));

      acc_0 = lmac8(acc_0, data_buf2, 2, 0x76543210, coeffs_rest, 0,
                    0x00000000); // l, 4*h, g
      acc_0 = lmac8(acc_0, data_buf2, 4, 0x76543210, coeffs_rest, 0,
                    0x00000000); // l, 4*h, g, i

      acc_0 = lmac8(acc_0, data_buf1, 3, 0x76543210, coeffs_rest, 0,
                    0x00000000); // l, 4*h, g, i, d

      // Calculates lap_ijp
      lap_0 = srs(acc_0, 0);

      // Calculates lap_ijp - lap_ij
      flux_sub =
          sub16(concat(lap_0, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32 *)out_flux2.data() + i;
      *ptr_out = ext_w(flux_sub, 0);

      //***********************************************************************STARTING
      // X
      // FLUX*****************************************************************************************************************************************************

      acc_1 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000); // g
      acc_0 = lmul8(data_buf2, 2, 0x76543210, coeffs_rest, 0, 0x00000000); // g

      row0_ptr = ((v8int32 *)(row0.data())) + i;
      data_buf2 = upd_w(data_buf2, 0, *(row0_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row0_ptr));

      acc_1 = lmsc8(acc_1, data_buf1, 2, 0x76543210, coeffs, 0,
                    0x00000000); // g, 4*c
      acc_1 = lmac8(acc_1, data_buf1, 1, 0x76543210, coeffs_rest, 0,
                    0x00000000); // g, 4*c, b
      acc_1 = lmac8(acc_1, data_buf2, 2, 0x76543210, coeffs_rest, 0,
                    0x00000000); // g, 4*c, b, a

      row4_ptr = ((v8int32 *)(row4.data())) + i;
      data_buf2 = upd_w(data_buf2, 0, *(row4_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row4_ptr));

      acc_1 = lmac8(acc_1, data_buf1, 3, 0x76543210, coeffs_rest, 0,
                    0x00000000); // g, 4*c, b, a, d
      acc_0 = lmac8(acc_0, data_buf2, 2, 0x76543210, coeffs_rest, 0,
                    0x00000000); // g, m

      // Calculates lap_imj
      lap_0 = srs(acc_1, 0);

      flux_sub =
          sub16(concat(lap_ij, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98,
                concat(lap_0, undef_v8int32()), 0, 0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32 *)out_flux3.data() + i;
      *ptr_out = ext_w(flux_sub, 0);

      row3_ptr = ((v8int32 *)(row3.data())) + i;
      data_buf1 = upd_w(data_buf1, 0, *(row3_ptr)++);
      data_buf1 = upd_w(data_buf1, 1, *(row3_ptr));

      acc_0 = lmsc8(acc_0, data_buf1, 2, 0x76543210, coeffs, 0,
                    0x00000000); // g, m , k * 4

      acc_0 = lmac8(acc_0, data_buf1, 1, 0x76543210, coeffs_rest, 0,
                    0x00000000); // g, m , k * 4, j

      // LOAD DATA FOR NEXT ITERATION
      row1_ptr = ((v8int32 *)(row1.data())) + i + 1;
      data_buf2 = upd_w(data_buf2, 0, *(row1_ptr)++);
      data_buf2 = upd_w(data_buf2, 1, *(row1_ptr));

      acc_0 = lmac8(acc_0, data_buf1, 3, 0x76543210, coeffs_rest, 0,
                    0x00000000); // g, m , k * 4, j, l

      flux_sub = sub16(concat(srs(acc_0, 0), undef_v8int32()), 0, 0x76543210,
                       0xFEDCBA98, concat(lap_ij, undef_v8int32()), 0,
                       0x76543210, 0xFEDCBA98);
      ptr_out = (v8int32 *)out_flux4.data() + i;
      *ptr_out = ext_w(flux_sub, 0);

      // LOAD DATA FOR NEXT ITERATION
      row3_ptr = ((v8int32 *)(row3.data())) + i + 1;
      data_buf1 = upd_w(data_buf1, 0, *(row3_ptr)++);
      data_buf1 = upd_w(data_buf1, 1, *(row3_ptr));

    }
}