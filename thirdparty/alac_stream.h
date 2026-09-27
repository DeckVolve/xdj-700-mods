#ifndef XDJ700_ALAC_STREAM_H
#define XDJ700_ALAC_STREAM_H

/*
 * Allocation-free ALAC container-to-PCM stream adapter.
 *
 * The caller owns the stream object, one reusable packet buffer, and the
 * decoder workspace.  The adapter parses the public container through
 * alac_container, reads one access unit at a time into that packet buffer,
 * and sends each decoded block to the existing neutral ALAC PCM sink.  It
 * has no filesystem assumptions and does not allocate memory.
 */

#include <stdint.h>

#include "alac_container.h"

/* Use this value when the caller has no finite application-chosen frame cap.
 * The stream still enforces container and decoder limits. */
#define ALAC_STREAM_UNLIMITED_FRAMES UINT64_MAX

enum {
    ALAC_STREAM_OK = 0,
    ALAC_STREAM_END = 1,
    ALAC_STREAM_ERR_ARGUMENT = 2,
    ALAC_STREAM_ERR_CONTAINER = 3,
    ALAC_STREAM_ERR_IO = 4,
    ALAC_STREAM_ERR_UNSUPPORTED = 5,
    ALAC_STREAM_ERR_PACKET = 6,
    ALAC_STREAM_ERR_WORKSPACE = 7,
    ALAC_STREAM_ERR_FRAME_LIMIT = 8,
    ALAC_STREAM_ERR_FRAME_COUNT = 9,
    ALAC_STREAM_ERR_DECODER = 10,
    ALAC_STREAM_ERR_OUTPUT = 11,
    /* Appended without changing the established result ABI. */
    ALAC_STREAM_ERR_RANGE = 12,
    ALAC_STREAM_ERR_STATE = 13
};

typedef struct {
    /* Public parsed state is retained so a fixed task object can inspect it. */
    alac_container container;

    /* Caller-owned reusable storage.  Only one packet is resident at once. */
    unsigned char *packet_storage;
    unsigned packet_capacity;
    void *workspace;
    unsigned workspace_capacity;

    /* Cumulative hard upper bound for delivered PCM frames.  A value of
     * ALAC_STREAM_UNLIMITED_FRAMES means no finite caller-chosen cap. */
    uint64_t max_frames;
    uint64_t next_packet;
    uint64_t frames_decoded;
    alac_container_cursor packet_cursor;

    /* Direct underlying statuses are retained for diagnostics without adding
     * another allocation or changing the compact public return codes. */
    /* Successful packet resolution resets this to ALAC_CONTAINER_OK. */
    int last_container_error;
    int last_decoder_error;
    /* Nonzero address-derived identity token; callers must not modify it. */
    unsigned opened;
} alac_stream;

/*
 * Parse source and prepare a stream.
 *
 * packet_storage is reused for every access unit and must be at least one
 * byte.  workspace_capacity must cover alac_mod_workspace_bytes() for the
 * parsed cookie.  max_frames must be nonzero and is a cumulative hard upper
 * bound on PCM frames delivered to the sink across this stream, not a
 * per-packet bound.  A declared container frame count larger than this value
 * is rejected before decoding begins; a packet that would cross the limit is
 * rejected without calling the caller's sink or advancing the packet cursor.
 * ALAC_STREAM_UNLIMITED_FRAMES removes only the caller-chosen finite cap.
 *
 * If stream is non-NULL, its object is cleared before argument validation, so
 * a failed open leaves it closed.  The source callback, its user object,
 * packet_storage, and workspace must remain valid until the stream is no
 * longer used.  The source itself is copied into the caller-owned
 * alac_container member.  Opening is synchronous and may call source->read_at.
 * Do not re-enter this stream from the source callback, and do not use the
 * same stream, packet storage, or workspace concurrently from another
 * thread.  A successful stream retains its copied source in
 * stream->container.source.  Passing that member back as source is a
 * supported allocation-free reopen/rewind operation: the implementation
 * preserves it before clearing the destination and starts again at packet
 * and frame zero.  A failed reopen leaves the stream closed apart from its
 * direct diagnostic status, so retry it with a separately retained source.
 * A successful object has stable identity: do not copy or move it before a
 * retained-source reopen or decode operation. Such a copy is rejected with
 * ERR_STATE before any source callback or PCM sink runs. packet_storage and
 * workspace must be disjoint valid address ranges and neither may overlap
 * stream. An unsafe range or alias returns ERR_ARGUMENT without modifying
 * stream, reading the source, or writing either storage region.
 */
int alac_stream_open(alac_stream *stream,
                     const alac_container_source *source,
                     unsigned char *packet_storage,
                     unsigned packet_capacity,
                     void *workspace,
                     unsigned workspace_capacity,
                     uint64_t max_frames);

/* Return nonzero only when stream is an open object at its original address.
 * This lets allocation-free outer bridges validate retained-source reopen
 * identity before clearing their enclosing object. It performs no I/O. */
int alac_stream_has_stable_identity(const alac_stream *stream);

/*
 * Decode and deliver one access unit.  The sink is called synchronously at
 * most once and only after the complete packet has decoded.  Its channels
 * array and sample planes are valid only while that callback is executing;
 * the sink must consume or copy them and must not retain, modify, or
 * re-enter this stream with them.  A successful call returns ALAC_STREAM_OK
 * and reports that packet's frame count.  ALAC_STREAM_END means that all
 * packets were delivered; frames_out is set to zero for END.  On a limit,
 * sink, or other output error the packet cursor is not advanced.  Calls on a
 * given stream are synchronous and must not overlap or recursively call one
 * another. Violations on the same object return ALAC_STREAM_ERR_STATE before
 * another source read or sink delivery. The retained packet cursor must be
 * initialized, its next_packet must equal the stream's mirrored next_packet,
 * and that value must not exceed the container packet count. A violation
 * returns ALAC_STREAM_ERR_STATE with frames_out zero and the stream unchanged,
 * before source I/O; equality with packet_count is the sole normal END case.
 * frames_out must not overlap the stream object, packet storage, or decoder
 * workspace. Such an alias returns ALAC_STREAM_ERR_ARGUMENT without modifying
 * the aliased bytes, reading the source, or invoking the sink, including when
 * another argument is invalid.
 */
int alac_stream_next(alac_stream *stream, alac_mod_pcm_sink sink,
                     void *sink_user, unsigned *frames_out);

/*
 * Decode all remaining packets.  frames_out reports frames delivered during
 * this call.  The sink has the same synchronous pointer-lifetime and
 * non-reentrancy contract as alac_stream_next().  ALAC_STREAM_OK means the
 * stream reached its end; an error leaves the stream positioned at the
 * packet that could not be delivered.  The call must not overlap or
 * recursively invoke another operation on the same stream. frames_out has
 * the same non-aliasing and transactional rejection contract as next(),
 * including when another argument is invalid.
 */
int alac_stream_decode(alac_stream *stream, alac_mod_pcm_sink sink,
                       void *sink_user, uint64_t *frames_out);

#endif
