#ifndef XDJ700_AUDIO_FILE_H
#define XDJ700_AUDIO_FILE_H

/*
 * Bounded-storage public-format file bridge.
 *
 * This module intentionally stops at a neutral PCM sink.  It recognizes a
 * native FLAC stream or a public CAF/ISO-BMFF ALAC container through a
 * caller-owned random-access source, then routes decoding through the
 * independently written codec layers.  It contains no XDJ address, task
 * message, or proprietary decoder ABI.
 *
 * The ALAC path uses only caller-owned storage.  The linked FLAC glue uses a
 * bounded process-wide bump arena and diagnostic status, so calls through
 * this bridge must be serialized and must not be nested or concurrent.
 */

#include <stdint.h>

#include "alac_stream.h"

#define AUDIO_FILE_UNLIMITED_FRAMES UINT64_MAX

typedef int (*audio_file_read_at)(void *user, uint64_t offset,
                                  void *destination, unsigned count,
                                  unsigned *bytes_read);

typedef struct {
    audio_file_read_at read_at;
    void *user;
    uint64_t size;
} audio_file_source;

/* The planes contain signed, right-aligned int32_t samples.  They are
 * decoder-owned and valid only during this synchronous callback: the sink
 * must consume or copy them before returning, must not modify or retain
 * them, and must not re-enter audio_file_decode() on the same object. */
typedef int (*audio_file_pcm_sink)(const int32_t * const channels[],
                                   unsigned frame_count,
                                   unsigned channel_count,
                                   unsigned bits_per_sample,
                                   void *user);

enum {
    AUDIO_FILE_KIND_UNKNOWN = 0,
    AUDIO_FILE_KIND_FLAC = 1,
    AUDIO_FILE_KIND_ALAC_CAF = 2,
    AUDIO_FILE_KIND_ALAC_ISOBMFF = 3
};

enum {
    AUDIO_FILE_OK = 0,
    AUDIO_FILE_ERR_ARGUMENT = 1,
    AUDIO_FILE_ERR_IO = 2,
    AUDIO_FILE_ERR_FORMAT = 3,
    AUDIO_FILE_ERR_UNSUPPORTED = 4,
    AUDIO_FILE_ERR_RANGE = 5,
    AUDIO_FILE_ERR_WORKSPACE = 6,
    AUDIO_FILE_ERR_FRAME_LIMIT = 7,
    AUDIO_FILE_ERR_FRAME_COUNT = 8,
    AUDIO_FILE_ERR_DECODER = 9,
    AUDIO_FILE_ERR_OUTPUT = 10,
    AUDIO_FILE_ERR_STATE = 11
};

typedef struct {
    unsigned kind;
    unsigned sample_rate;
    unsigned channels;
    unsigned bits_per_sample;
    uint64_t total_frames;
    /* FLAC metadata end offset; zero for ALAC containers. */
    uint64_t data_offset;
} audio_file_info;

/* The bridge owns no source, packet, or decoder storage.  Every pointed-to
 * object must remain valid for the lifetime of the opened file and any
 * synchronous decode call.  An open ALAC object has stable identity: do not
 * copy or move it. Decode on a moved ALAC copy returns AUDIO_FILE_ERR_STATE
 * before a source read or sink callback. */
typedef struct {
    audio_file_source source;
    audio_file_info info;
    /* Zero is closed, one is a fresh FLAC stream, two is a consumed FLAC
     * stream, and three is an open ALAC stream. */
    unsigned state;

    /* The format alternatives never coexist.  FLAC retains only its frame
     * cap here; ALAC retains the complete stream.  The ALAC stream itself
     * owns the copied exact-read adapter and caller-storage pointers. */
    union {
        uint64_t flac_max_frames;
        alac_stream alac;
    } decoder;
} audio_file;

/* Probe and validate a public-format source without decoding it.  Probe uses
 * the same non-reentrant bridge ownership as open/decode: a nested call from
 * any source or PCM callback returns AUDIO_FILE_ERR_STATE before clearing
 * info or reading the source. source and info must not overlap; an alias
 * returns AUDIO_FILE_ERR_ARGUMENT without modifying either object or reading
 * the source. */
int audio_file_probe(const audio_file_source *source, audio_file_info *info);

/* Parse and prepare a file.  ALAC requires the caller-owned packet buffer and
 * decoder workspace; FLAC ignores those two arguments.  max_frames is a
 * cumulative hard limit, or AUDIO_FILE_UNLIMITED_FRAMES. source may be
 * &file->source to reopen/rewind an already open object; it is retained before
 * the destination is cleared. A retained-source reopen on a shallow moved
 * ALAC copy returns STATE without modifying it or reading the source.
 * packet_storage and workspace, when supplied, must be disjoint valid ranges
 * and neither may overlap file; violations return ARGUMENT transactionally.
 * Same-object callback recursion returns STATE without clearing the active
 * file. */
int audio_file_open(audio_file *file, const audio_file_source *source,
                    unsigned char *packet_storage, unsigned packet_capacity,
                    void *workspace, unsigned workspace_capacity,
                    uint64_t max_frames);

/* Decode all remaining data synchronously into the neutral sink.  On an
 * error, frames_out reports frames accepted by the sink during this call.
 * The FLAC bridge is one-shot because it does not retain libFLAC decoder
 * state in the caller-owned file object; a second FLAC call returns
 * AUDIO_FILE_ERR_STATE instead of replaying the stream.  ALAC retains its
 * packet cursor, so calls after end return zero frames successfully.
 * frames_out must not overlap the file object or ALAC packet/workspace
 * storage. Such an alias returns AUDIO_FILE_ERR_ARGUMENT without modifying
 * the aliased bytes, reading the source, or invoking the sink. */
int audio_file_decode(audio_file *file, audio_file_pcm_sink sink,
                      void *sink_user, uint64_t *frames_out);

#endif
