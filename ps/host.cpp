#include "TopGraph.h"
#include "./ProcessUnit/include.h"

#include <adf/adf_api/XRTConfig.h>
#include <experimental/xrt_device.h>
#include <experimental/xrt_kernel.h>
// ⚠️ 彻底删除了引发报错的 xrt_aie.h

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

TopStencilGraph topStencil;

namespace {

constexpr int NUM_INPUTS = 5;
constexpr int DEFAULT_ITER = 2;
constexpr int PREVIEW = 16;

bool load_stream_file(const std::string& path, int32_t* buf, int elems) {
    std::ifstream fin(path);
    if (!fin.is_open()) {
        std::fprintf(stderr, "[warn] cannot open %s\n", path.c_str());
        return false;
    }

    long long v = 0;
    int cnt = 0;
    while (fin >> v) {
        if (cnt >= elems) break;
        buf[cnt++] = static_cast<int32_t>(v);
    }

    if (cnt != elems) {
        std::fprintf(stderr,
                     "[warn] %s element count mismatch: got %d, expect %d\n",
                     path.c_str(), cnt, elems);
        return false;
    }
    return true;
}

void fill_ramp_inputs(int32_t* inbuf[NUM_INPUTS], int elems_per_input) {
    for (int k = 0; k < NUM_INPUTS; ++k) {
        for (int i = 0; i < elems_per_input; ++i) {
            inbuf[k][i] = static_cast<int32_t>(k * 10000 + i);
        }
    }
}

void zero_output(int32_t* out, int elems) {
    for (int i = 0; i < elems; ++i) out[i] = 0;
}

void dump_output_matrix(const std::string& path, const int32_t* out, int iter_cnt) {
    std::ofstream fout(path);
    if (!fout.is_open()) {
        std::fprintf(stderr, "[warn] cannot open %s for write\n", path.c_str());
        return;
    }

    for (int it = 0; it < iter_cnt; ++it) {
        for (int c = 0; c < COL; ++c) {
            if (c) fout << ' ';
            fout << out[it * COL + c];
        }
        fout << '\n';
    }
}

void print_preview(const char* tag, const int32_t* p, int n) {
    std::printf("%s", tag);
    for (int i = 0; i < n; ++i) {
        std::printf(" %d", p[i]);
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage: %s <xclbin> [iter_cnt] [input_prefix] [output_txt]\n",
                     argv[0]);
        return EXIT_FAILURE;
    }

    const std::string xclbin_path = argv[1];
    const int iter_cnt            = (argc >= 3) ? std::atoi(argv[2]) : DEFAULT_ITER;
    const std::string in_prefix   = (argc >= 4) ? argv[3] : "./data/hdiff";
    const std::string out_path    = (argc >= 5) ? argv[4] : "./data/aie_out_gmio.txt";

    if (iter_cnt <= 0) {
        std::fprintf(stderr, "[error] iter_cnt must be > 0\n");
        return EXIT_FAILURE;
    }

    const int elems_per_input = iter_cnt * COL;
    const int out_elems       = iter_cnt * COL;
    const std::size_t bytes_per_input = elems_per_input * sizeof(int32_t);
    const std::size_t out_bytes       = out_elems * sizeof(int32_t);

    auto dhdl = xrtDeviceOpen(0);
    if (!dhdl) {
        std::fprintf(stderr, "[error] xrtDeviceOpen failed\n");
        return EXIT_FAILURE;
    }

    int ret = xrtDeviceLoadXclbinFile(dhdl, xclbin_path.c_str());
    if (ret) {
        std::fprintf(stderr, "[error] xrtDeviceLoadXclbinFile failed\n");
        xrtDeviceClose(dhdl);
        return EXIT_FAILURE;
    }

    xuid_t uuid;
    xrtDeviceGetXclbinUUID(dhdl, uuid);
    adf::registerXRT(dhdl, uuid);

    int32_t* inbuf[NUM_INPUTS] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < NUM_INPUTS; ++i) {
        inbuf[i] = reinterpret_cast<int32_t*>(adf::GMIO::malloc(bytes_per_input));
        if (!inbuf[i]) {
            std::fprintf(stderr, "[error] GMIO::malloc failed for input %d\n", i);
            for (int j = 0; j < i; ++j) adf::GMIO::free(inbuf[j]);
            xrtDeviceClose(dhdl);
            return EXIT_FAILURE;
        }
    }

    int32_t* outbuf = reinterpret_cast<int32_t*>(adf::GMIO::malloc(out_bytes));
    if (!outbuf) {
        std::fprintf(stderr, "[error] GMIO::malloc failed for output\n");
        for (int i = 0; i < NUM_INPUTS; ++i) adf::GMIO::free(inbuf[i]);
        xrtDeviceClose(dhdl);
        return EXIT_FAILURE;
    }

    bool ok = true;
    for (int i = 0; i < NUM_INPUTS; ++i) {
        const std::string path = in_prefix + "_in" + std::to_string(i) + "_stream.txt";
        if (!load_stream_file(path, inbuf[i], elems_per_input)) {
            ok = false;
        }
    }
    if (!ok) {
        std::fprintf(stderr, "[warn] input files incomplete, fallback to ramp input\n");
        fill_ramp_inputs(inbuf, elems_per_input);
    }

    zero_output(outbuf, out_elems);
    print_preview("input0 preview:", inbuf[0], PREVIEW);

    // ==========================================
    // 核心执行与性能测量区域 (2023.2 adf::event API)
    // ==========================================
    topStencil.init();

    // 补回丢失的起点 t0
    auto t0 = std::chrono::high_resolution_clock::now();

    // 1) 先启动 graph
    topStencil.run(iter_cnt);

    // 2) 先挂输出
    topStencil.out0.aie2gm_nb(outbuf, out_bytes);

    // 3) 启动原生的 ADF event profiling 探针
    adf::event::handle handle = adf::event::start_profiling(
        topStencil.out0,
        adf::event::io_stream_start_to_bytes_transferred_cycles,
        static_cast<uint32_t>(out_bytes));

    if (handle == adf::event::invalid_handle) {
        std::fprintf(stderr,
                     "[error] invalid profiling handle "
                     "(likely no available performance counters on this interface tile)\n");
        topStencil.end();
        for (int i = 0; i < NUM_INPUTS; ++i) {
            adf::GMIO::free(inbuf[i]);
        }
        adf::GMIO::free(outbuf);
        xrtDeviceClose(dhdl);
        return EXIT_FAILURE;
    }

    // 4) 再发 5 路输入，开始喂数据
    topStencil.in0.gm2aie_nb(inbuf[0], bytes_per_input);
    topStencil.in1.gm2aie_nb(inbuf[1], bytes_per_input);
    topStencil.in2.gm2aie_nb(inbuf[2], bytes_per_input);
    topStencil.in3.gm2aie_nb(inbuf[3], bytes_per_input);
    topStencil.in4.gm2aie_nb(inbuf[4], bytes_per_input);

    // 5) 等待 DMA 传输和 Graph 计算彻底完成
    topStencil.out0.wait();
    topStencil.wait();

    // 6) 读回精确的硬件时钟周期，并关闭探针
    long long cycle_count = adf::event::read_profiling(handle);
    adf::event::stop_profiling(handle);

    // 7) 按照你 Makefile 里的 450 MHz 计算真实的吞吐量 (1 cycle = 1/450000000 秒)
    const double freq_hz = 450000000.0;
    const double time_seconds = static_cast<double>(cycle_count) / freq_hz;
    
    // 1 MB = 1024 * 1024 Bytes
    const double output_MBps = static_cast<double>(out_bytes) / time_seconds / (1024.0 * 1024.0);
    
    const double gross_bytes = static_cast<double>(NUM_INPUTS) * static_cast<double>(bytes_per_input) +
                               static_cast<double>(out_bytes);
    const double gross_MBps = gross_bytes / time_seconds / (1024.0 * 1024.0);

    std::printf("========================================\n");
    std::printf("Event API cycles        : %lld\n", cycle_count);
    std::printf("Output throughput       : %.3f MB/s\n", output_MBps);
    std::printf("Gross graph throughput  : %.3f MB/s\n", gross_MBps);
    std::printf("========================================\n");

    // 记录软件秒表的终点 t1
    auto t1 = std::chrono::high_resolution_clock::now();
    topStencil.end();
    // ==========================================

    const auto dur_us =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    std::printf("End-to-end time: %lld us\n", static_cast<long long>(dur_us));
    print_preview("output preview:", outbuf, PREVIEW);
    dump_output_matrix(out_path, outbuf, iter_cnt);

    for (int i = 0; i < NUM_INPUTS; ++i) {
        adf::GMIO::free(inbuf[i]);
    }
    adf::GMIO::free(outbuf);
    xrtDeviceClose(dhdl);
    return 0;
}