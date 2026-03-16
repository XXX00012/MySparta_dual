#include "TopGraph.h"

TopStencilGraph topStencil;

#if defined(__AIESIM__) || defined(__X86SIM__) || defined(__ADF_FRONTEND__)
int main(int argc, char** argv) {
    topStencil.init();
    topStencil.run(2);
    topStencil.end();
    return 0;
}
#endif