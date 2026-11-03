/*
 * DoubleMonumentFinderBase standalone CLI.
 *
 * The CLI is intentionally a thin layer over include/doublemonumentfinder/base.h:
 * argument parsing, progress display and the text report live here; the
 * search itself is exactly the same code path used by the GUI's JNI shim.
 *
 * Usage: DoubleMonumentFinderBase-cli --help
 */

#include "doublemonumentfinder/base.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr const char *kCliVersion = DMF_BASE_VERSION_STRING;
constexpr int kDefaultSquareRegions = 256; /* GUI default square side (regions) */

volatile std::sig_atomic_t g_interrupted = 0;

void handle_interrupt(int)
{
    g_interrupted = 1;
}

struct Options
{
    int64_t seed = 0;
    bool seed_set = false;

    int32_t min_x = -kDefaultSquareRegions / 2;
    int32_t max_x = min_x + kDefaultSquareRegions;
    int32_t min_z = min_x;
    int32_t max_z = max_x;
    bool bounds_set = false;
    bool square_set = false;
    int64_t square = 0;

    int32_t threads = 0;
    int32_t mc = 0;
    bool mc_set = false;
    std::string mc_text;

    int64_t limit = 0;
    std::string out_path;
    bool quiet = false;

    bool help = false;
    bool version = false;
};

void print_usage(std::ostream &os)
{
    os <<
        "DoubleMonumentFinderBase standalone CLI\n"
        "\n"
        "Finds pairs of ocean monuments for a fixed Minecraft seed.\n"
        "Search bounds use region coordinates (512 blocks per region), exactly\n"
        "like the GUI: [min-x, max-x) x [min-z, max-z).\n"
        "\n"
        "Usage:\n"
        "  DoubleMonumentFinderBase-cli --seed <int64> [options]\n"
        "\n"
        "Options:\n"
        "  --seed <int64>       World seed (required).\n"
        "  --min-x <int>        Minimum region X (inclusive).\n"
        "  --max-x <int>        Maximum region X (exclusive).\n"
        "  --min-z <int>        Minimum region Z (inclusive).\n"
        "  --max-z <int>        Maximum region Z (exclusive).\n"
        "  --square <int>       Square side in regions, centered at (0,0).\n"
        "                       Default: 256. Cannot be mixed with min/max.\n"
        "  --threads <int>      Worker threads; 0 = CPU cores (max 256).\n"
        "  --mc <version|id>    Minecraft version, e.g. 26.3 or 1.21.4.\n"
        "                       Default: newest supported (26.3).\n"
        "  --limit <int>        Maximum output rows; 0 = all (default).\n"
        "  --out <file>         Write the same text report to a .txt file.\n"
        "  --quiet              Do not print the live progress line.\n"
        "  --help, -h           Show this help.\n"
        "  --version            Show CLI and API versions.\n"
        "\n"
        "Examples:\n"
        "  DoubleMonumentFinderBase-cli --seed 7 --square 256 --out result.txt\n"
        "  DoubleMonumentFinderBase-cli --seed -12345 --min-x -64 --max-x 64 \\\n"
        "      --min-z -64 --max-z 64 --threads 8 --limit 50 --out pair.txt\n";
}

bool parse_i64(const char *text, int64_t &out)
{
    if (!text || !*text)
        return false;
    errno = 0;
    char *end = nullptr;
    const long long value = std::strtoll(text, &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return false;
    out = static_cast<int64_t>(value);
    return true;
}

bool parse_i32(const char *text, int32_t &out)
{
    int64_t value = 0;
    if (!parse_i64(text, value) || value < INT32_MIN || value > INT32_MAX)
        return false;
    out = static_cast<int32_t>(value);
    return true;
}

bool take_value(int argc, char **argv, int &index, const char *name,
                const char *&value, std::string &error)
{
    if (index + 1 >= argc) {
        error = std::string("missing value for ") + name;
        return false;
    }
    value = argv[++index];
    return true;
}

bool parse_options(int argc, char **argv, Options &opt, std::string &error)
{
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!std::strcmp(arg, "--help") || !std::strcmp(arg, "-h")) {
            opt.help = true;
            return true;
        }
        if (!std::strcmp(arg, "--version")) {
            opt.version = true;
            return true;
        }
        if (!std::strcmp(arg, "--quiet")) {
            opt.quiet = true;
            continue;
        }

        const char *value = nullptr;
        if (!std::strcmp(arg, "--seed")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i64(value, opt.seed)) {
                error = "invalid --seed";
                return false;
            }
            opt.seed_set = true;
        } else if (!std::strcmp(arg, "--min-x")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i32(value, opt.min_x)) {
                error = "invalid --min-x";
                return false;
            }
            opt.bounds_set = true;
        } else if (!std::strcmp(arg, "--max-x")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i32(value, opt.max_x)) {
                error = "invalid --max-x";
                return false;
            }
            opt.bounds_set = true;
        } else if (!std::strcmp(arg, "--min-z")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i32(value, opt.min_z)) {
                error = "invalid --min-z";
                return false;
            }
            opt.bounds_set = true;
        } else if (!std::strcmp(arg, "--max-z")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i32(value, opt.max_z)) {
                error = "invalid --max-z";
                return false;
            }
            opt.bounds_set = true;
        } else if (!std::strcmp(arg, "--square")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i64(value, opt.square) || opt.square < 1) {
                error = "invalid --square (must be >= 1)";
                return false;
            }
            opt.square_set = true;
        } else if (!std::strcmp(arg, "--threads")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i32(value, opt.threads)) {
                error = "invalid --threads";
                return false;
            }
        } else if (!std::strcmp(arg, "--mc")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            opt.mc_text = value;
            opt.mc_set = true;
        } else if (!std::strcmp(arg, "--limit")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            if (!parse_i64(value, opt.limit) || opt.limit < 0 || opt.limit > INT32_MAX) {
                error = "invalid --limit (must be 0..2147483647)";
                return false;
            }
        } else if (!std::strcmp(arg, "--out")) {
            if (!take_value(argc, argv, i, arg, value, error))
                return false;
            opt.out_path = value;
        } else {
            error = std::string("unknown option: ") + arg;
            return false;
        }
    }
    return true;
}

bool finalize_options(Options &opt, std::string &error)
{
    if (opt.help || opt.version)
        return true;
    if (!opt.seed_set) {
        error = "--seed is required";
        return false;
    }
    if (opt.square_set && opt.bounds_set) {
        error = "--square cannot be combined with --min-x/--max-x/--min-z/--max-z";
        return false;
    }
    if (opt.square_set) {
        if (opt.square > INT32_MAX) {
            error = "--square is too large";
            return false;
        }
        const int64_t min = -opt.square / 2;
        const int64_t max = min + opt.square;
        if (min < INT32_MIN || max > INT32_MAX) {
            error = "--square does not fit in 32-bit region coordinates";
            return false;
        }
        opt.min_x = static_cast<int32_t>(min);
        opt.max_x = static_cast<int32_t>(max);
        opt.min_z = opt.min_x;
        opt.max_z = opt.max_x;
    }
    if (opt.min_x >= opt.max_x || opt.min_z >= opt.max_z) {
        error = "search region must satisfy min-x < max-x and min-z < max-z";
        return false;
    }
    if (opt.threads < 0 || opt.threads > DMF_MAX_THREADS) {
        error = "--threads must be 0 (auto) or 1..256";
        return false;
    }
    if (opt.mc_set) {
        opt.mc = dmf_parse_mc(opt.mc_text.c_str());
        if (opt.mc < 0) {
            error = "unsupported --mc value: " + opt.mc_text;
            return false;
        }
    } else {
        opt.mc = dmf_newest_mc();
    }
    return true;
}

const char *ocean_label(int type)
{
    switch (type) {
    case 1:
        return "cold";
    case 2:
        return "frozen";
    default:
        return "others";
    }
}

bool hit_less(const DmfPairHit &a, const DmfPairHit &b)
{
    if (a.hang_x != b.hang_x)
        return a.hang_x < b.hang_x;
    if (a.hang_z != b.hang_z)
        return a.hang_z < b.hang_z;
    if (a.dx != b.dx)
        return a.dx < b.dx;
    if (a.dz != b.dz)
        return a.dz < b.dz;
    return a.low_efficiency < b.low_efficiency;
}

std::string format_report(const Options &opt, int64_t elapsed_ms,
                          const std::vector<DmfPairHit> &hits, int32_t found)
{
    std::ostringstream os;
    os << "# DoubleMonumentFinderBase CLI " << kCliVersion << "\n";
    os << "# seed=" << opt.seed
       << " region=[" << opt.min_x << "," << opt.max_x << ")x["
       << opt.min_z << "," << opt.max_z << ")"
       << " threads=" << opt.threads
       << " mc=" << dmf_mc_name(opt.mc) << "(" << opt.mc << ")"
       << " found=" << found
       << " output=" << hits.size()
       << " elapsed=" << (elapsed_ms / 1000.0) << "s\n";
    os << "# columns: hangX hangZ dx dz lowEfficiency oceanType tp\n";
    os << "hangX\thangZ\tdx\tdz\tlow\tocean\ttp\n";
    for (const DmfPairHit &hit : hits) {
        const int ocean = dmf_classify_ocean_type(
            opt.seed, opt.mc, hit.hang_x, hit.hang_z);
        os << hit.hang_x << '\t'
           << hit.hang_z << '\t'
           << hit.dx << '\t'
           << hit.dz << '\t'
           << hit.low_efficiency << '\t'
           << ocean_label(ocean) << '\t'
           << "/tp " << hit.hang_x << " 50 " << hit.hang_z << "\n";
    }
    return os.str();
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;
    std::string error;
    if (!parse_options(argc, argv, opt, error)) {
        std::fprintf(stderr, "error: %s\n\n", error.c_str());
        print_usage(std::cerr);
        return 2;
    }
    if (opt.help) {
        print_usage(std::cout);
        return 0;
    }
    if (opt.version) {
        std::cout << "DoubleMonumentFinderBase-cli " << kCliVersion << "\n"
                  << "DoubleMonumentFinderBase API " << DMF_BASE_VERSION_STRING << "\n";
        return 0;
    }
    if (!finalize_options(opt, error)) {
        std::fprintf(stderr, "error: %s\n\n", error.c_str());
        print_usage(std::cerr);
        return 2;
    }

    std::signal(SIGINT, handle_interrupt);

    DmfSearchParams params{};
    params.seed = opt.seed;
    params.min_x = opt.min_x;
    params.max_x = opt.max_x;
    params.min_z = opt.min_z;
    params.max_z = opt.max_z;
    params.mc = opt.mc;
    params.threads = opt.threads;
    params.pause_on_pair_found = 0;

    std::atomic<bool> search_done{false};
    std::thread watchdog([&]() {
        while (!search_done.load()) {
            if (g_interrupted) {
                dmf_stop();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    std::thread progress;
    if (!opt.quiet) {
        progress = std::thread([&]() {
            while (!search_done.load()) {
                int64_t p[3] = {0, 0, 0};
                dmf_get_progress(p);
                if (p[2] == DMF_STATUS_RUNNING && p[1] > 0) {
                    const double pct = 100.0 * static_cast<double>(p[0]) /
                                       static_cast<double>(p[1]);
                    std::fprintf(stderr, "\r[search] %lld / %lld (%.1f%%)",
                                 static_cast<long long>(p[0]),
                                 static_cast<long long>(p[1]), pct);
                    std::fflush(stderr);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
    }

    const auto start = std::chrono::steady_clock::now();
    const int rc = dmf_run_search(&params);
    const auto end = std::chrono::steady_clock::now();
    const int64_t elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    search_done.store(true);
    watchdog.join();
    if (progress.joinable())
        progress.join();
    if (!opt.quiet)
        std::fprintf(stderr, "\r%*s\r", 48, "");

    if (rc < 0) {
        std::fprintf(stderr, "search failed: %s\n", dmf_last_error());
        return 1;
    }
    if (rc == DMF_CANCELLED) {
        std::fprintf(stderr, "search cancelled\n");
        return 4;
    }

    const int32_t found = dmf_result_count();
    std::vector<DmfPairHit> hits;
    if (found > 0) {
        hits.resize(static_cast<size_t>(found));
        const int32_t copied = dmf_get_results(hits.data(), found);
        if (copied < 0) {
            std::fprintf(stderr, "search failed: %s\n", dmf_last_error());
            return 1;
        }
        hits.resize(static_cast<size_t>(copied));
    }
    std::sort(hits.begin(), hits.end(), hit_less);
    if (opt.limit > 0 && static_cast<int64_t>(hits.size()) > opt.limit)
        hits.resize(static_cast<size_t>(opt.limit));

    const std::string report = format_report(opt, elapsed_ms, hits, found);
    std::cout << report;
    std::cout.flush();

    if (!opt.out_path.empty()) {
        std::ofstream out(opt.out_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "error: cannot open --out file: %s\n",
                         opt.out_path.c_str());
            return 1;
        }
        out.write(report.data(), static_cast<std::streamsize>(report.size()));
        out.flush();
        if (!out) {
            std::fprintf(stderr, "error: failed to write --out file: %s\n",
                         opt.out_path.c_str());
            return 1;
        }
        std::fprintf(stderr, "report written to %s\n", opt.out_path.c_str());
    }

    return 0;
}
