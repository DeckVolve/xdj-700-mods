#ifndef XDJ700_ALAC_CONTAINER_H
#define XDJ700_ALAC_CONTAINER_H

/*
 * Allocation-free ALAC container boundary.
 *
 * This module knows only public CAF and ISO base media file structure.  It
 * locates the ALAC codec cookie and packet table, but deliberately leaves
 * packet decoding to alac_mod_decode_packet().  All storage is supplied by a
 * caller-owned random-access reader, which is also the shape needed by the
 * eventual XDJ file-task adapter.
 */

#include <stdint.h>

#include "alac_mod.h"

/*
 * The legacy container callback is an exact-read contract: returning zero
 * means that all count bytes were written to destination.  It is kept for
 * source compatibility with existing callers.
 */
typedef int (*alac_container_read_at)(void *user, uint64_t offset,
                                      void *destination, unsigned count);

/*
 * Adapter contract for storage APIs that report the number of bytes
 * supplied.  A callback may return zero with a short read; callers must use
 * alac_container_read_at_exact() when handing this callback to a container.
 */
typedef int (*alac_container_read_at_partial)(
    void *user, uint64_t offset, void *destination, unsigned count,
    unsigned *bytes_read);

typedef struct {
    alac_container_read_at_partial read_at;
    void *user;
} alac_container_partial_reader;

/*
 * Strict adapter for alac_container_source.read_at.  It returns zero only
 * when the partial reader succeeds and supplies exactly count bytes.  The
 * alac_container_partial_reader object must remain alive for every read
 * performed through the resulting alac_container_source.  The destination
 * may not overlap that retained adapter object. A nonempty offset/count range
 * crossing the 64-bit address ceiling is rejected before the partial reader
 * is called.
 */
int alac_container_read_at_exact(void *user, uint64_t offset,
                                  void *destination, unsigned count);

typedef struct {
    alac_container_read_at read_at;
    void *user;
    uint64_t size;
} alac_container_source;

enum {
    ALAC_CONTAINER_OK = 0,
    ALAC_CONTAINER_ERR_ARGUMENT = 1,
    ALAC_CONTAINER_ERR_IO = 2,
    ALAC_CONTAINER_ERR_FORMAT = 3,
    ALAC_CONTAINER_ERR_UNSUPPORTED = 4,
    ALAC_CONTAINER_ERR_OVERFLOW = 5,
    ALAC_CONTAINER_ERR_PACKET = 6,
    ALAC_CONTAINER_ERR_WORK_LIMIT = 7
};

enum {
    ALAC_CONTAINER_KIND_CAF = 1,
    ALAC_CONTAINER_KIND_ISOBMFF = 2
};

typedef struct {
    uint64_t offset;
    unsigned length;
} alac_container_packet;

/* Caller-owned state for an allocation-free, forward-only packet walk.  The
 * fields are public only so the cursor can live inside a fixed task object;
 * callers must initialize it with alac_container_cursor_init() and advance it
 * only with alac_container_cursor_next(). */
typedef struct {
    uint64_t next_packet;
    uint64_t next_offset;
    uint64_t caf_sizes_cursor;
    unsigned mp4_chunk;
    unsigned mp4_sample_in_chunk;
    unsigned mp4_samples_per_chunk;
    unsigned mp4_stsc_run;
    unsigned mp4_next_run_first_chunk;
    unsigned initialized;
} alac_container_cursor;

/* The structure is caller-owned and contains only offsets/counts into the
 * source.  It is intentionally public so it can live in a fixed task object
 * without requiring malloc. */
typedef struct {
    alac_container_source source;
    alac_mod_config config;
    unsigned kind;
    unsigned have_config;
    unsigned have_tables;

    uint64_t packet_count;
    uint64_t total_frames;

    /* CAF packet table and data payload. */
    /* Nonzero for a constant-size CAF packet stream.  In that form the
     * packet-size BER table is absent and packet offsets are derived. */
    unsigned caf_constant_bytes_per_packet;
    /* Number of unused frames at the end of the final packet.  This is zero
     * for containers without a CAF remainder declaration. */
    unsigned caf_remainder_frames;
    uint64_t caf_packet_sizes;
    uint64_t caf_packet_sizes_end;
    uint64_t caf_data;
    uint64_t caf_data_end;

    /* ISO BMFF sample tables.  Entry offsets point at the first table entry,
     * after each atom's version/flags and count fields.  A nonzero
     * mp4_stz2_field_size selects the compact 4/8/16-bit stz2 table; zero
     * selects the ordinary stsz table (including its fixed-size form). */
    uint64_t mp4_stsc_entries;
    uint64_t mp4_stco_entries;
    uint64_t mp4_stsz_entries;
    unsigned mp4_stsc_count;
    unsigned mp4_chunk_count;
    unsigned mp4_sample_count;
    unsigned mp4_sample_description;
    unsigned mp4_fixed_sample_size;
    unsigned mp4_stz2_field_size;
    unsigned mp4_co64;

    /* The selected audio sample entry must use dref index 1, and that
     * reference must be a self-contained `url ` entry. */
    unsigned mp4_have_dref;
    unsigned mp4_dref_self_contained;

    /* ISO BMFF timeline checks.  These fields are parser state rather than
     * playback metadata: this boundary accepts only an audio track whose
     * media timebase is the ALAC PCM rate and whose edit list is identity. */
    unsigned mp4_have_mdhd;
    unsigned mp4_media_timescale;
    unsigned mp4_have_stts;
    uint64_t mp4_stts_sample_count;
    uint64_t mp4_stts_duration;
    unsigned mp4_have_movie_header;
    unsigned mp4_movie_timescale;
    unsigned mp4_have_edit_list;
    uint64_t mp4_edit_duration;
} alac_container;

/* Parse and validate a bounded container.  ISO-BMFF open verifies that every
 * selected ALAC sample extent is present inside a top-level mdat range, but
 * does not read compressed packet payload.  A non-NULL container is cleared
 * before validation and remains closed after every failure.  The source may
 * be &container->source, which provides an allocation-free reopen operation
 * using the previously retained source.  Every other source overlap with the
 * destination, and every wrapped source or destination range, is rejected
 * before source inspection or destination modification. */
int alac_container_open(const alac_container_source *source,
                        alac_container *container);

/* Open with a caller-selected processing budget.  One unit is charged for
 * each source callback attempt and each fixed-size ISO-BMFF sample visited
 * without a source read.  No more than max_work units are performed; zero
 * permits no reads.  Optional work_used reports the consumed units, including
 * a source operation that fails.  Exhaustion returns ERR_WORK_LIMIT; other
 * results retain the ordinary open semantics.  This bounds parser work, not
 * time spent inside a caller's read callback.  The original source is retained
 * in container only on success, and later packet operations are outside this
 * opening budget.  Like alac_container_open(), every failure leaves a non-NULL
 * container closed and the source may alias container->source.  work_used
 * must be disjoint from source and container; unsafe aliases and wrapped
 * ranges are rejected before any of those bytes are modified. */
int alac_container_open_bounded(const alac_container_source *source,
                                alac_container *container,
                                unsigned max_work, unsigned *work_used);

/* Find the byte range of one ALAC access unit.  The returned range remains
 * valid until the next source operation and is bounded by source.size.  The
 * output may not overlap the retained container. */
int alac_container_packet_at(const alac_container *container,
                             uint64_t index,
                             alac_container_packet *packet);

/* Initialize and advance a forward-only packet cursor.  Each successful next
 * call resolves exactly one packet and advances the cursor.  A failed call
 * leaves the cursor unchanged.  Cursor and packet outputs may not overlap
 * each other or the retained container.  This is the streaming interface:
 * unlike packet_at(), it never rescans earlier packet-size entries. */
int alac_container_cursor_init(const alac_container *container,
                               alac_container_cursor *cursor);
int alac_container_cursor_next(const alac_container *container,
                               alac_container_cursor *cursor,
                               alac_container_packet *packet);

/* Read one packet into caller-owned storage.  The byte destination and length
 * output may not overlap each other or the retained container. */
int alac_container_read_packet(const alac_container *container,
                               uint64_t index, unsigned char *destination,
                               unsigned capacity, unsigned *length_out);

#endif
