/*
 * DoubleMonumentFinderBase — public API implementation.
 *
 * This translation unit is only glue: it owns the process-wide search state
 * and forwards the actual work to the original double-monument kernel
 * (src/monument_search.cpp + src/monument_search_core.c).
 */

#include "doublemonumentfinder/base.h"

#include "Thread.h"
#include "monument_search.h"
#include "monument_search_core.h"

#include "cubiomes/biomes.h"
#include "cubiomes/util.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

std::mutex g_run_mutex;
std::mutex g_error_mutex;
std::string g_last_error;

Progress g_progress{};
ThreadSafeResults<PairHit> g_results{};

bool is_valid_mc(int mc)
{
    return mc > MC_UNDEF && mc <= MC_NEWEST;
}

void set_error(const char *message)
{
    std::lock_guard<std::mutex> lock(g_error_mutex);
    g_last_error = message ? message : "";
}

const char *valid_mc_name(int mc)
{
    if (!is_valid_mc(mc))
        return "unknown";
    const char *name = mc2str(mc);
    if (!name || !*name || !std::strcmp(name, "?"))
        return "unknown";
    return name;
}

} // namespace

int dmf_run_search(const DmfSearchParams *params)
{
    if (!params) {
        set_error("params must not be null");
        return DMF_ERR_PARAM;
    }
    if (params->min_x >= params->max_x || params->min_z >= params->max_z) {
        set_error("search region must satisfy min_x < max_x and min_z < max_z");
        return DMF_ERR_PARAM;
    }
    if (!is_valid_mc(params->mc)) {
        set_error("unsupported cubiomes MCVersion");
        return DMF_ERR_PARAM;
    }
    if (params->threads < 0 || params->threads > DMF_MAX_THREADS) {
        set_error("threads must be 0 (auto) or 1..256");
        return DMF_ERR_PARAM;
    }

    std::lock_guard<std::mutex> run_lock(g_run_mutex);

    set_error("");
    g_results.clear();
    g_progress.current = 0;
    g_progress.total = 0;
    g_progress.status = DMF_STATUS_IDLE;
    g_progress.try_pause = false;
    g_progress.try_stop = false;

    MonumentSearchConfig cfg;
    cfg.seed = static_cast<uint64_t>(params->seed);
    cfg.minX = params->min_x;
    cfg.maxX = params->max_x;
    cfg.minZ = params->min_z;
    cfg.maxZ = params->max_z;
    cfg.mc = params->mc;
    cfg.pauseOnPairFound = params->pause_on_pair_found != 0;

    int32_t threads = params->threads;
    if (threads == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        threads = hw > 0
            ? static_cast<int32_t>(std::min<unsigned>(hw, DMF_MAX_THREADS))
            : 1;
    }

    runMonumentSearch(cfg, &g_progress, g_results, threads);

    if (g_progress.try_stop.load())
        return DMF_CANCELLED;
    return DMF_OK;
}

int dmf_pause(void)
{
    g_progress.try_pause = true;
    return DMF_OK;
}

int dmf_resume(void)
{
    g_progress.try_pause = false;
    return DMF_OK;
}

int dmf_stop(void)
{
    g_progress.try_stop = true;
    return DMF_OK;
}

void dmf_reset(void)
{
    g_progress.try_pause = false;
    g_progress.try_stop = false;
}

void dmf_get_progress(int64_t out[3])
{
    if (!out)
        return;
    out[0] = g_progress.current.load();
    out[1] = g_progress.total.load();
    out[2] = g_progress.status.load();
}

const char *dmf_last_error(void)
{
    std::lock_guard<std::mutex> lock(g_error_mutex);
    return g_last_error.c_str();
}

int32_t dmf_result_count(void)
{
    return static_cast<int32_t>(g_results.size());
}

int32_t dmf_get_results(DmfPairHit *out, int32_t capacity)
{
    if (capacity < 0 || (!out && capacity > 0))
        return DMF_ERR_PARAM;

    const std::vector<PairHit> hits = g_results.peekAll();
    const int32_t total = static_cast<int32_t>(hits.size());
    const int32_t count = std::min(total, capacity);

    for (int32_t i = 0; i < count; ++i) {
        const PairHit &src = hits[static_cast<size_t>(i)];
        DmfPairHit &dst = out[i];
        dst.hang_x = src.hangX;
        dst.hang_z = src.hangZ;
        dst.dx = src.dx;
        dst.dz = src.dz;
        dst.low_efficiency = src.lowEfficiency;
    }
    return total;
}

int dmf_classify_ocean_type(int64_t seed, int32_t mc, int32_t center_x, int32_t center_z)
{
    if (!is_valid_mc(mc))
        return 0;
    return monument_classify_ocean_type(mc, static_cast<uint64_t>(seed), center_x, center_z);
}

int dmf_classify_ocean_types(int64_t seed, int32_t mc, int32_t center_x, int32_t center_z)
{
    if (!is_valid_mc(mc))
        return 0;
    MonumentGenerator *g = monument_generator_alloc();
    if (!g)
        return 0;
    monument_init_generator(g, mc, static_cast<uint64_t>(seed));
    const int mask = monument_classify_ocean_types_g(g, mc, center_x, center_z);
    monument_generator_free(g);
    return mask;
}

int dmf_parse_mc(const char *text)
{
    if (!text)
        return -1;

    std::string value(text);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    if (value.empty())
        return -1;

    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "latest" || lower == "newest")
        return MC_NEWEST;

    errno = 0;
    char *end = nullptr;
    const long numeric = std::strtol(value.c_str(), &end, 10);
    if (errno == 0 && end && *end == '\0') {
        if (numeric > MC_UNDEF && numeric <= MC_NEWEST)
            return static_cast<int>(numeric);
        return -1;
    }

    const int mc = str2mc(value.c_str());
    return is_valid_mc(mc) ? mc : -1;
}

const char *dmf_mc_name(int mc)
{
    return valid_mc_name(mc);
}

int dmf_newest_mc(void)
{
    return MC_NEWEST;
}
