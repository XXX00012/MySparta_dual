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
    kernel k_flux;

    StencilCoreGraph() {
#if defined(__AIESIM__) || defined(__X86SIM__) || defined(__ADF_FRONTEND__)
        k_lap   = kernel::create(hdiff_lap);
        k_flux  = kernel::create(hdiff_flux);

        source(k_lap)   = "ProcessUnit/hdiff_lap.cc";
        source(k_flux)  = "ProcessUnit/hdiff_flux.cc";

        headers(k_lap)   = {"ProcessUnit/hdiff.h", "ProcessUnit/include.h"};
        headers(k_flux)  = {"ProcessUnit/hdiff.h", "ProcessUnit/include.h"};

        runtime<ratio>(k_lap)   = 0.9;
        runtime<ratio>(k_flux)  = 0.9;

        location<kernel>(k_lap)   = tile(7, 1);
        location<kernel>(k_flux)  = tile(7, 2);

        connect(in[0], k_lap.in[0]);
        connect(in[1], k_lap.in[1]);
        connect(in[2], k_lap.in[2]);
        connect(in[3], k_lap.in[3]);
        connect(in[4], k_lap.in[4]);

        connect(in[1], k_flux.in[0]);
        connect(in[2], k_flux.in[1]);
        connect(in[3], k_flux.in[2]);

        connect(k_lap.out[0], k_flux.in[3]);
        connect(k_lap.out[1], k_flux.in[4]);
        connect(k_lap.out[2], k_flux.in[5]);
        connect(k_lap.out[3], k_flux.in[6]);

        connect(k_flux.out[0], out);

        for (int i = 0; i < 5; ++i) {
            dimensions(in[i])       = {COL};
            dimensions(k_lap.in[i]) = {COL};
        }

        dimensions(k_flux.in[0]) = {COL};
        dimensions(k_flux.in[1]) = {COL};
        dimensions(k_flux.in[2]) = {COL};

        for (int i = 0; i < 4; ++i) {
            dimensions(k_lap.out[i])      = {COL + 4};
            dimensions(k_flux.in[i + 3])  = {COL + 4};
        }

        dimensions(k_flux.out[0]) = {COL + 8};
        dimensions(out)           = {COL + 8};
#endif

    }
};
