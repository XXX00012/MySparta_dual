#include "TopGraph.h"
#include <adf.h>
#include <fstream>

TopStencilGraph topStencil;

#if defined(__AIESIM__) || defined(__X86SIM__) || defined(__ADF_FRONTEND__)
int main(int argc, char** argv) {
    int ITER = 64;
    const int BLOCK_SIZE = 1024*ITER;

    topStencil.init();

    int32* dinArray[5];
    int32* doutArray[1];
    

    for(int i=0;i<5;i++){
        dinArray[i]=(int32*)GMIO::malloc(BLOCK_SIZE);
    }
    doutArray[0]=(int32*)GMIO::malloc(BLOCK_SIZE);
    std::cout<<"GMIO::malloc completed"<<std::endl;

    const char* inFiles[5] = {
    "./data/hdiff_in0_stream.txt",
    "./data/hdiff_in1_stream.txt",
    "./data/hdiff_in2_stream.txt",
    "./data/hdiff_in3_stream.txt",
    "./data/hdiff_in4_stream.txt"
};

for(int i=0;i<5;i++){
    std::ifstream fin(inFiles[i]);
    if(!fin.is_open()){
        std::cout<<"cannot open "<<inFiles[i]<<std::endl;
        return 1;
    }

    for(int j=0;j<BLOCK_SIZE/sizeof(int32);j++){
        fin >> dinArray[i][j];
    }

    fin.close();
    }

    topStencil.run(ITER);

    topStencil.in0.gm2aie_nb(dinArray[0],BLOCK_SIZE);
    topStencil.in1.gm2aie_nb(dinArray[1],BLOCK_SIZE);
    topStencil.in2.gm2aie_nb(dinArray[2],BLOCK_SIZE);
    topStencil.in3.gm2aie_nb(dinArray[3],BLOCK_SIZE);
    topStencil.in4.gm2aie_nb(dinArray[4],BLOCK_SIZE);
    
    topStencil.out0.aie2gm_nb(doutArray[0],BLOCK_SIZE);
    topStencil.out0.wait();
   

    for(int i=0;i<COL;i++){
        std::cout<<doutArray[0][i]<<" ";
    }
    std::cout<<std::endl;



    topStencil.end();

    
    return 0;
}
#endif