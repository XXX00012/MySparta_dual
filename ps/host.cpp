#include "TopGraph.h"
#include "./ProcessUnit/include.h"

#include <adf/adf_api/XRTConfig.h>
#include <experimental/xrt_device.h>
#include <experimental/xrt_kernel.h>

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
constexpr int OUTPUT_DATA_WORDS = COL;
constexpr int OUTPUT_META_WORDS = 8;  // lap start/end + flux start/end
constexpr int OUTPUT_RECORD_WORDS = OUTPUT_DATA_WORDS + OUTPUT_META_WORDS;
constexpr double AIE_FREQ_HZ = 450000000.0;

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

bool load_stream_file(const std::string& path, int32_t* buf, int elems, int stride_words) {
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
        const int32_t* row = out + it * OUTPUT_RECORD_WORDS;
        for (int c = 0; c < COL; ++c) {
            if (c) fout << ' ';
            fout << row[c];
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

inline uint64_t load_u64_from_i32_pair(const int32_t* p) {
    const uint64_t lo = static_cast<uint64_t>(static_cast<uint32_t>(p[0]));
    const uint64_t hi = static_cast<uint64_t>(static_cast<uint32_t>(p[1]));
    return lo | (hi << 32);
}

void summarize_cycles(const char* name, const uint64_t* cycles, int iter_cnt) {
    uint64_t total = 0;
    uint64_t max_v = 0;
    uint64_t min_v = (iter_cnt > 0) ? cycles[0] : 0;

    for (int i = 0; i < iter_cnt; ++i) {
        total += cycles[i];
        if (cycles[i] > max_v) max_v = cycles[i];
        if (cycles[i] < min_v) min_v = cycles[i];
    }

    const double avg_cycles = (iter_cnt > 0) ? static_cast<double>(total) / iter_cnt : 0.0;
    const double total_us = static_cast<double>(total) * 1.0e6 / AIE_FREQ_HZ;
    const double avg_us = avg_cycles * 1.0e6 / AIE_FREQ_HZ;

    std::printf("%s cycles total      : %llu\n", name, static_cast<unsigned long long>(total));
    std::printf("%s cycles avg/iter   : %.3f\n", name, avg_cycles);
    std::printf("%s cycles min/iter   : %llu\n", name, static_cast<unsigned long long>(min_v));
    std::printf("%s cycles max/iter   : %llu\n", name, static_cast<unsigned long long>(max_v));
    std::printf("%s time total        : %.3f us\n", name, total_us);
    std::printf("%s time avg/iter     : %.3f us\n", name, avg_us);
}

void print_cycle_preview(const char* tag, const uint64_t* cycles, int n) {
    std::printf("%s", tag);
    for (int i = 0; i < n; ++i) {
        std::printf(" %llu", static_cast<unsigned long long>(cycles[i]));
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
    const int out_elems       = iter_cnt * OUTPUT_RECORD_WORDS;
    const std::size_t bytes_per_input = elems_per_input * sizeof(int32_t);
    const std::size_t out_bytes       = static_cast<std::size_t>(out_elems) * sizeof(int32_t);

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

    topStencil.init();
    auto t0 = std::chrono::high_resolution_clock::now();

    topStencil.run(iter_cnt);
    topStencil.out0.aie2gm_nb(outbuf, out_bytes);

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

    topStencil.in0.gm2aie_nb(inbuf[0], bytes_per_input);
    topStencil.in1.gm2aie_nb(inbuf[1], bytes_per_input);
    topStencil.in2.gm2aie_nb(inbuf[2], bytes_per_input);
    topStencil.in3.gm2aie_nb(inbuf[3], bytes_per_input);
    topStencil.in4.gm2aie_nb(inbuf[4], bytes_per_input);

    topStencil.out0.wait();
    topStencil.wait();

    long long cycle_count = adf::event::read_profiling(handle);
    adf::event::stop_profiling(handle);

    auto t1 = std::chrono::high_resolution_clock::now();
    topStencil.end();

    const double time_seconds = static_cast<double>(cycle_count) / AIE_FREQ_HZ;
    const double output_MBps = static_cast<double>(out_bytes) / time_seconds / (1024.0 * 1024.0);
    const double gross_bytes = static_cast<double>(NUM_INPUTS) * static_cast<double>(bytes_per_input) +
                               static_cast<double>(out_bytes);
    const double gross_MBps = gross_bytes / time_seconds / (1024.0 * 1024.0);
    const auto dur_us =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    uint64_t* lap_cycles = new uint64_t[iter_cnt];
    uint64_t* flux_cycles = new uint64_t[iter_cnt];
    for (int it = 0; it < iter_cnt; ++it) {
        const int32_t* rec = outbuf + it * OUTPUT_RECORD_WORDS;
        const uint64_t lap_start  = load_u64_from_i32_pair(rec + OUTPUT_DATA_WORDS + 0);
        const uint64_t lap_end    = load_u64_from_i32_pair(rec + OUTPUT_DATA_WORDS + 2);
        const uint64_t flux_start = load_u64_from_i32_pair(rec + OUTPUT_DATA_WORDS + 4);
        const uint64_t flux_end   = load_u64_from_i32_pair(rec + OUTPUT_DATA_WORDS + 6);
        lap_cycles[it] = lap_end - lap_start;
        flux_cycles[it] = flux_end - flux_start;
    }

    std::printf("========================================\n");
    std::printf("Graph output event cycles : %lld\n", cycle_count);
    std::printf("Output throughput         : %.3f MB/s\n", output_MBps);
    std::printf("Gross graph throughput    : %.3f MB/s\n", gross_MBps);
    std::printf("End-to-end time           : %lld us\n", static_cast<long long>(dur_us));
    std::printf("========================================\n");

    summarize_cycles("lap core", lap_cycles, iter_cnt);
    summarize_cycles("flux core", flux_cycles, iter_cnt);

    const int preview_n = (iter_cnt < PREVIEW) ? iter_cnt : PREVIEW;
    print_cycle_preview("lap cycle preview:", lap_cycles, preview_n);
    print_cycle_preview("flux cycle preview:", flux_cycles, preview_n);

    print_preview("output preview:", outbuf, PREVIEW);
    dump_output_matrix(out_path, outbuf, iter_cnt);

    delete[] lap_cycles;
    delete[] flux_cycles;

    for (int i = 0; i < NUM_INPUTS; ++i) {
        adf::GMIO::free(inbuf[i]);
    }
    adf::GMIO::free(outbuf);
    xrtDeviceClose(dhdl);
    return 0;
}
