/*
 * DoubleMonumentFinderBase — public C API
 * ============================================================================
 *
 * Stable contract between this library and its consumers:
 *
 *   - DoubleMonumentForFixedSeed (the Swing GUI) talks to the library through
 *     a thin JNI shim (jni/Project_MonumentFinderBridge.cpp) that only forwards
 *     to the functions declared here.
 *   - The standalone DoubleMonumentFinderBase-cli executable uses exactly the
 *     same API, so the GUI and the CLI always run the same search core.
 *
 * The implementation lives in src/base.cpp and the algorithm files under src/.
 * Consumers must only include this header; the internal search files are not
 * part of the contract.
 *
 * Threading:
 *   - dmf_run_search() is synchronous and must run on a worker thread.
 *   - dmf_get_progress(), dmf_pause(), dmf_resume(), dmf_stop(),
 *     dmf_reset(), dmf_result_count() and dmf_get_results() may be called from
 *     other threads while dmf_run_search() is executing.
 *   - Only one search may run at a time; a second dmf_run_search() call blocks
 *     on the internal API mutex until the first one finishes.
 *
 * Coordinates:
 *   - Search bounds are region coordinates in 512-block units, exactly like
 *     the GUI: [min_x, max_x) x [min_z, max_z).
 *   - Result coordinates (hang_x/hang_z/dx/dz) are block coordinates.
 */

#ifndef DOUBLEMONUMENTFINDER_BASE_H
#define DOUBLEMONUMENTFINDER_BASE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DMF_BASE_VERSION_MAJOR 1
#define DMF_BASE_VERSION_MINOR 0
#define DMF_BASE_VERSION_PATCH 0
#define DMF_BASE_VERSION_STRING "1.0.0"

/* Search progress status values (compatible with the GUI progress bar). */
enum
{
    DMF_STATUS_IDLE = 0,
    DMF_STATUS_RUNNING = 1,
    DMF_STATUS_DONE = 2
};

/* Return values. */
enum
{
    DMF_OK = 0,
    DMF_CANCELLED = 1,
    DMF_ERR_PARAM = -1,
    DMF_ERR_BUSY = -2,
    DMF_ERR_STATE = -3,
    DMF_ERR_ALLOC = -4,
    DMF_ERR_INTERNAL = -5
};

/* Ocean temperature bits returned by dmf_classify_ocean_types(). */
#define DMF_OCEAN_MASK_FROZEN 0x01
#define DMF_OCEAN_MASK_COLD 0x02
#define DMF_OCEAN_MASK_NORMAL 0x04
#define DMF_OCEAN_MASK_LUKEWARM 0x08
#define DMF_OCEAN_MASK_WARM 0x10

#define DMF_MAX_THREADS 256

typedef struct DmfSearchParams
{
    int64_t seed;

    /* Region coordinates, max values exclusive. */
    int32_t min_x;
    int32_t max_x;
    int32_t min_z;
    int32_t max_z;

    /* cubiomes MCVersion (e.g. 35 for 26.3). */
    int32_t mc;

    /* 0 = hardware_concurrency(), otherwise 1..DMF_MAX_THREADS. */
    int32_t threads;

    /* 1 = pause the workers as soon as a non-low-efficiency pair is found. */
    int32_t pause_on_pair_found;
} DmfSearchParams;

typedef struct DmfPairHit
{
    int32_t hang_x;          /* midpoint of the pair, block X */
    int32_t hang_z;          /* midpoint of the pair, block Z */
    int32_t dx;              /* |x2 - x1| in blocks */
    int32_t dz;              /* |z2 - z1| in blocks */
    int32_t low_efficiency;  /* 1 = valid pair with slightly reduced efficiency */
} DmfPairHit;

/*
 * Runs one search synchronously on the calling thread (worker threads are
 * spawned internally). On return every hit has been written to the result
 * container.
 *
 * Returns DMF_OK, DMF_CANCELLED when dmf_stop() was requested, or a negative
 * error code (see dmf_last_error()).
 */
int dmf_run_search(const DmfSearchParams *params);

int dmf_pause(void);
int dmf_resume(void);
int dmf_stop(void);

/* Clears the pause/stop flags. Does not clear results or progress. */
void dmf_reset(void);

/*
 * out must have 3 elements: [0]=processed regions, [1]=total regions,
 * [2]=status (DMF_STATUS_*).
 */
void dmf_get_progress(int64_t out[3]);

/* Last error message; empty when no error occurred. */
const char *dmf_last_error(void);

int32_t dmf_result_count(void);

/*
 * Copies up to `capacity` hits to `out` and returns the total number of hits
 * currently available. Pass out=NULL, capacity=0 to query the count.
 */
int32_t dmf_get_results(DmfPairHit *out, int32_t capacity);

/*
 * Legacy primary ocean type at the pair center:
 * 0=others, 1=cold, 2=frozen.
 */
int dmf_classify_ocean_type(int64_t seed, int32_t mc, int32_t center_x, int32_t center_z);

/*
 * All ocean temperature types among the four check points (+-64 blocks around
 * the center) as a bitmask of DMF_OCEAN_MASK_*; 0 when none is ocean.
 */
int dmf_classify_ocean_types(int64_t seed, int32_t mc, int32_t center_x, int32_t center_z);

/* ---- cubiomes-backed version helpers (CLI / future GUI version picker) ---- */

/* Resolves "26.3", "1.21.4", "latest" or a raw cubiomes MCVersion number. */
int dmf_parse_mc(const char *text);

/* Canonical version name for an MCVersion, e.g. "26.3"; "unknown" if invalid. */
const char *dmf_mc_name(int mc);

/* Newest supported cubiomes MCVersion. */
int dmf_newest_mc(void);

#ifdef __cplusplus
}
#endif

#endif /* DOUBLEMONUMENTFINDER_BASE_H */
