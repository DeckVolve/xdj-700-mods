#ifndef XDJ700_DUAL_FORMAT_PAYLOAD_H
#define XDJ700_DUAL_FORMAT_PAYLOAD_H

/*
 * Neutral FLAC/ALAC payload boundary.
 *
 * This is an in-tree public-format C API. It intentionally
 * knows nothing about Pioneer firmware, filesystem objects, task messages,
 * PCM packing, update images, or flash addresses. A caller supplies random
 * access reads and consumes signed, right-aligned PCM planes synchronously.
 * It is therefore a codec payload contract, not a stock integration ABI.
 * FLAC uses caller-owned arena storage through a bounded process-wide
 * allocator and status word. Same-thread reentrant calls are rejected, but
 * the owner checks are non-atomic: true preemptive concurrency requires
 * caller task confinement or an external lock around every payload entry.
 * The caller retains ownership of every storage region.
 */

#include <stdint.h>

#include "audio_file.h"
#include "flac_mod.h"

#define DUAL_FORMAT_PAYLOAD_FLAC_ARENA_SIZE FLAC_MOD_ARENA_SIZE
#define DUAL_FORMAT_PAYLOAD_FLAC_ARENA_ALIGNMENT FLAC_MOD_ARENA_ALIGNMENT
#define DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_SIZE \
    DUAL_FORMAT_PAYLOAD_FLAC_ARENA_SIZE
#define DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT \
    DUAL_FORMAT_PAYLOAD_FLAC_ARENA_ALIGNMENT
#define DUAL_FORMAT_PAYLOAD_ALAC_WORKSPACE_SIZE \
    ALAC_MOD_MAX_WORKSPACE_BYTES
#define DUAL_FORMAT_PAYLOAD_ALAC_PACKET_SIZE \
    (DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_SIZE - \
     DUAL_FORMAT_PAYLOAD_ALAC_WORKSPACE_SIZE)

/* Return zero for a completed read and set bytes_read to the byte count.
 * Positive short reads are permitted; the format bridge retries them when it
 * requires an exact read.  A successful zero-byte read below source.size is a
 * stalled input and is rejected as I/O; source.size itself defines EOF.  A
 * nonzero return is an input/I/O error. */
typedef audio_file_read_at dual_format_payload_read_at;

/* PCM planes are decoder-owned and valid only for this synchronous call.
 * The sink must consume or copy them before returning, and must not retain,
 * modify, or use them to re-enter this entry point on the same storage. */
typedef audio_file_pcm_sink dual_format_payload_pcm_sink;

typedef struct {
    dual_format_payload_read_at read_at;
    void *read_user;
    uint64_t size;
} dual_format_payload_source;

/* All decoder storage is caller-owned. Native FLAC requires flac_arena to be
 * DUAL_FORMAT_PAYLOAD_FLAC_ARENA_ALIGNMENT-aligned and flac_arena_capacity to
 * be at least DUAL_FORMAT_PAYLOAD_FLAC_ARENA_SIZE. CAF/ISO-BMFF ALAC ignores
 * the FLAC fields and retains its existing packet/workspace requirements.
 * Every region and this descriptor must remain valid from session open
 * through close and must not be shared with another active session. The
 * descriptor itself is mandatory for both formats. max_frames is a
 * cumulative hard cap; use
 * AUDIO_FILE_UNLIMITED_FRAMES only with no finite cap. */
typedef struct {
    void *flac_arena;
    unsigned flac_arena_capacity;
    unsigned char *alac_packet_storage;
    unsigned alac_packet_capacity;
    void *alac_workspace;
    unsigned alac_workspace_capacity;
    uint64_t max_frames;
} dual_format_payload_storage;

typedef audio_file_info dual_format_payload_info;

/* An open session owns one source and one shared-storage descriptor until
 * close.  The members are exposed only so callers can reserve the object
 * without allocation; they are private state and must not be inspected,
 * copied, moved, or modified while the session is active. */
typedef struct {
    audio_file file;
    flac_mod_session flac;
    const dual_format_payload_source *source;
    const dual_format_payload_storage *storage;
    uint64_t frames_delivered;
    uint64_t flac_offset;
    unsigned state;
    unsigned operation_active;
    unsigned flac_opened;
} dual_format_payload_session;

/* Keep the caller-owned persistent session small enough for the currently
 * audited 32-bit SH-4 object budget.  This is an object-size guard, not proof
 * of sufficient device task stack.  The one-shot convenience API places one
 * such object on its own stack; device integration should prefer persistent
 * caller-owned session storage until live stack high-water is measured. */
#define DUAL_FORMAT_PAYLOAD_SH4_SESSION_MAX_BYTES 512u
#if UINTPTR_MAX == UINT32_MAX
typedef char dual_format_payload_sh4_session_size_must_fit[
    sizeof(dual_format_payload_session) <=
        DUAL_FORMAT_PAYLOAD_SH4_SESSION_MAX_BYTES ? 1 : -1];
#endif

enum {
    DUAL_FORMAT_PAYLOAD_NEXT_END = 0,
    DUAL_FORMAT_PAYLOAD_NEXT_BLOCK = 1
};

/* session_next reports existing AUDIO_FILE_ERR_* values as their negative
 * counterpart, keeping END and BLOCK distinct from the established positive
 * one-shot error ABI. */
#define DUAL_FORMAT_PAYLOAD_NEXT_ERROR(error_code) (-(int)(error_code))

/* Bind one caller-owned byte range for either supported format.  The helper
 * aligns within the supplied range, so a native allocator may provide only
 * four-byte alignment when capacity includes up to alignment-1 bytes of
 * leading slack.  FLAC uses the complete 128 KiB aligned region.  ALAC uses
 * the same region as a non-overlapping maximum decoder workspace followed by
 * a bounded packet buffer.  Those views intentionally alias across format
 * alternatives and therefore remain subject to the external-serialization
 * and non-reentrancy rule; they are never used simultaneously by one decode.
 *
 * On failure, storage is cleared. AUDIO_FILE_ERR_ARGUMENT reports a missing
 * pointer, descriptor, or zero frame limit. AUDIO_FILE_ERR_WORKSPACE reports
 * insufficient aligned capacity or address overflow. */
int dual_format_payload_bind_shared_storage(
    dual_format_payload_storage *storage, void *memory,
    unsigned memory_capacity, uint64_t max_frames);

/* Acquire the process-wide payload owner before the first source read, parse
 * and validate one FLAC/CAF-ALAC/ISO-BMFF-ALAC source, and initialize the
 * retained decoder state.  source, storage, their callback/user state, and
 * all backing regions must remain valid and unmodified until close.  A
 * same-thread nested open returns AUDIO_FILE_ERR_STATE without reading either
 * source. Preemptive callers must first serialize externally; racing opens
 * have no supported result. session and info_out must not overlap each other,
 * either descriptor, or a backing storage region. An unsafe writable alias
 * returns AUDIO_FILE_ERR_ARGUMENT without modifying the aliased bytes or
 * reading the source. A safe info_out is cleared on entry and populated only
 * when open returns AUDIO_FILE_OK. */
int dual_format_payload_session_open(
    dual_format_payload_session *session,
    const dual_format_payload_source *source,
    const dual_format_payload_storage *storage,
    dual_format_payload_info *info_out);

/* Deliver at most one decoder-owned PCM block synchronously: one FLAC frame
 * (via process_single) or one ALAC packet.  BLOCK reports a nonzero
 * frames_delivered value; END reports zero exactly once.  Errors are negative
 * AUDIO_FILE_ERR_* values and make the session terminal when caused by input,
 * decoding, frame policy, or the sink.  Calls after END/error fail closed
 * with -AUDIO_FILE_ERR_STATE.  The sink must consume/copy its planes before
 * returning and must not retain them. frames_delivered must not overlap the
 * session, retained descriptors, or any backing storage region; an unsafe
 * alias returns -AUDIO_FILE_ERR_ARGUMENT without modifying it, reading the
 * source, or invoking the sink. */
int dual_format_payload_session_next(
    dual_format_payload_session *session,
    dual_format_payload_pcm_sink sink,
    void *sink_user,
    unsigned *frames_delivered);

/* Release decoder resources and process-wide ownership.  Safe after open,
 * END, error, or cancellation; repeated close calls are harmless. During an
 * active callback, close on the exact owner or an interior pointer into its
 * session, retained descriptors, or backing storage is a transactional
 * no-op and cannot invalidate the live decode stack. */
void dual_format_payload_session_close(
    dual_format_payload_session *session);

/*
 * Open and completely decode one native FLAC or public CAF/ISO-BMFF ALAC
 * source. Return values are AUDIO_FILE_* values. info_out and frames_out are
 * optional and are cleared before any input read; info_out is populated after
 * a successful open and frames_out records PCM accepted by sink. The two
 * outputs must not overlap each other, either input descriptor, or a backing
 * storage region. An unsafe alias returns AUDIO_FILE_ERR_ARGUMENT without
 * clearing either output, reading the source, or invoking the sink.
 *
 * This binary/signature-compatible convenience entry is implemented as
 * session_open(), a session_next() loop, and session_close() on every path.
 * Its post-open failure metadata behavior is deliberately the session API's
 * documented behavior: info_out remains clear unless open fully succeeds.
 * ALAC does not inspect or initialize the FLAC arena. The external
 * serialization/non-reentrancy rule covers both session and one-shot entries.
 * This function does not discover or invoke any device ABI, and successful
 * linking/execution is not evidence that a firmware image can be installed
 * or flashed to hardware.
 */
int dual_format_payload_decode(const dual_format_payload_source *source,
                               const dual_format_payload_storage *storage,
                               dual_format_payload_pcm_sink sink,
                               void *sink_user,
                               dual_format_payload_info *info_out,
                               uint64_t *frames_out);

#endif
