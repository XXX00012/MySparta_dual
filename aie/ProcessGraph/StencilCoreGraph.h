#pragma once
#include <adf.h>
#include "../ProcessUnit/include.h"
#include "../ProcessUnit/hdiff.h"

using namespace adf;

class StencilCoreGraph : public graph {
public:
    port<input>  in[5];
    port<output> out;

    kernel k_lap;
    kernel k_flux1;
    kernel k_flux2;

    StencilCoreGraph() {
#if defined(__AIESIM__) || defined(__X86SIM__) || defined(__ADF_FRONTEND__)
        k_lap   = kernel::create(hdiff_lap);
        k_flux1 = kernel::create(hdiff_flux1);
        k_flux2 = kernel::create(hdiff_flux2);

        source(k_lap)   = "ProcessUnit/hdiff_lap.cc";
        source(k_flux1) = "ProcessUnit/hdiff_flux1.cc";
        source(k_flux2) = "ProcessUnit/hdiff_flux2.cc";

        headers(k_lap)   = {"ProcessUnit/hdiff.h", "ProcessUnit/include.h"};
        headers(k_flux1) = {"ProcessUnit/hdiff.h", "ProcessUnit/include.h"};
        headers(k_flux2) = {"ProcessUnit/hdiff.h", "ProcessUnit/include.h"};

        runtime<ratio>(k_lap)   = 0.9;
        runtime<ratio>(k_flux1) = 0.9;
        runtime<ratio>(k_flux2) = 0.9;

        location<kernel>(k_lap)   = tile(7, 1);
        location<kernel>(k_flux1) = tile(7, 2);
        location<kernel>(k_flux2) = tile(7, 3);

        connect(in[0], k_lap.in[0]);
        connect(in[1], k_lap.in[1]);
        connect(in[2], k_lap.in[2]);
        connect(in[3], k_lap.in[3]);
        connect(in[4], k_lap.in[4]);

        connect(in[1], k_flux1.in[0]);
        connect(in[2], k_flux1.in[1]);
        connect(in[3], k_flux1.in[2]);

        connect(k_lap.out[0], k_flux1.in[3]);
        connect(k_lap.out[1], k_flux1.in[4]);
        connect(k_lap.out[2], k_flux1.in[5]);
        connect(k_lap.out[3], k_flux1.in[6]);

        connect(k_flux1.out[0], k_flux2.in[0]);
        connect(k_flux1.out[1], k_flux2.in[1]);
        connect(k_flux1.out[2], k_flux2.in[2]);
        connect(k_flux1.out[3], k_flux2.in[3]);
        connect(k_flux1.out[4], k_flux2.in[4]);

        connect(k_flux2.out[0], out);

        for (int i = 0; i < 5; ++i) {
            dimensions(in[i])       = {COL};
            dimensions(k_lap.in[i]) = {COL};
        }

        dimensions(k_flux1.in[0]) = {COL};
        dimensions(k_flux1.in[1]) = {COL};
        dimensions(k_flux1.in[2]) = {COL};

        for (int i = 0; i < 4; ++i) {
            dimensions(k_lap.out[i])      = {COL};
            dimensions(k_flux1.in[i + 3]) = {COL};
        }

        for (int i = 0; i < 5; ++i) {
            dimensions(k_flux1.out[i]) = {2 * COL};
            dimensions(k_flux2.in[i])  = {2 * COL};
        }

        dimensions(k_flux2.out[0]) = {COL};
        dimensions(out)            = {COL};
#endif
    }
};