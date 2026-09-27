#ifndef XDJ700_RETAINED_FORMAT_ROUTER_H
#define XDJ700_RETAINED_FORMAT_ROUTER_H

/*
 * Clean-room routing policy for public audio file formats.
 *
 * This allocation-free module makes only a routing decision.  It does not
 * decode media, inspect filenames, know a device ABI, or call stock code.
 * A later integration adapter can send STOCK results to the existing path
 * and BRIDGE results to the independently written retained WAVE bridge.
 *
 * Classification is deliberately strict and bridge-fail-closed:
 *
 *   - a bounded RIFF/WAVE file is routed to the stock WAVE path;
 *   - a native FLAC stream in the retained bridge's production envelope
 *     (44.1/48 kHz, stereo, 16/24-bit, known length) is bridge-eligible;
 *   - a structurally bounded, self-contained ISO-BMFF audio track with one
 *     `mp4a`/`esds` sample description is identified as stock MP4A without
 *     claiming that its unparsed ES descriptor necessarily selects AAC;
 *   - the equivalent single `alac` sample description and public
 *     ALACSpecificConfig is bridge-eligible when it is 44.1/48 kHz,
 *     stereo, and 16/24-bit;
 *   - the detailed policy reports a positively recognized ALAC sample entry
 *     that fails the retained support envelope or full container preflight as
 *     REJECT_ALAC; unrelated, unreadable, and unrecognized inputs defer.
 *
 * In particular, finding the bytes "alac" or "fLaC" at an arbitrary offset
 * is never sufficient.  ALAC is accepted only at the public `stsd` sample
 * entry boundary, and FLAC only at byte zero with a valid STREAMINFO-first
 * metadata chain.
 */

#include <stdint.h>

typedef int (*retained_format_router_read_at)(
    void *user, uint64_t offset, void *destination, unsigned count,
    unsigned *bytes_read);

typedef struct {
    /*
     * Return zero for a completed source operation and set bytes_read to the
     * number of initialized bytes.  Short successful reads are permitted by
     * the callback contract but cause classification to reject because all
     * router reads are exact.  The callback must not retain destination.
     */
    retained_format_router_read_at read_at;
    void *user;
    uint64_t size;
} retained_format_router_source;

typedef enum {
    RETAINED_FORMAT_ROUTE_DEFER_STOCK = 0,
    RETAINED_FORMAT_ROUTE_STOCK_WAVE = 1,
    RETAINED_FORMAT_ROUTE_STOCK_MP4A = 2,
    RETAINED_FORMAT_ROUTE_BRIDGE_FLAC = 3,
    RETAINED_FORMAT_ROUTE_BRIDGE_ALAC = 4
} retained_format_route;

/* Kept outside the enum so existing exhaustive-switch callers retain source
 * compatibility.  The cast is a valid retained_format_route result and the
 * appended value preserves the numeric ABI of every existing route. */
#define RETAINED_FORMAT_ROUTE_REJECT_ALAC ((retained_format_route)5)

/*
 * Return the complete integration policy.  REJECT_ALAC means that an ALAC
 * sample entry was found at the public ISO-BMFF stsd boundary, but retained
 * support or preflight rejected it.  An ALAC-enabled integration must treat
 * this result as terminal: it must not retry the same source as stock MP4A or
 * AAC.  The result does not authorize decoding and does not imply that the
 * malformed or unsupported source is otherwise valid.
 */
retained_format_route retained_format_router_classify_detailed(
    const retained_format_router_source *source);

/* Diagnostic admission policy.  The structural recognizer remains bounded
 * by its existing box/metadata limits.  After positive ALAC recognition,
 * require this exact source size (zero disables the size restriction), then
 * bound the complete container preflight by max_alac_work.  Work units are
 * those documented by alac_container_open_bounded().  A size mismatch or
 * exhausted preflight returns REJECT_ALAC, never provisional BRIDGE_ALAC.
 * WAVE, FLAC, stock MP4A, and unrecognized-input routing is unchanged. */
retained_format_route retained_format_router_classify_detailed_bounded(
    const retained_format_router_source *source, uint64_t required_alac_size,
    unsigned max_alac_work);

/*
 * Classify one stable random-access source.  The function performs no
 * allocation, has no global mutable state, reads only bounded metadata, and
 * never reads compressed media payload.  Only BRIDGE_FLAC and BRIDGE_ALAC may
 * divert a source into retained code.  Every other result, including
 * DEFER_STOCK, must preserve the pre-existing stock dispatch unchanged; this
 * prevents the router from suppressing MP3, AIFF, or any stock format it does
 * not understand.  This compatibility entry point preserves the original
 * 0--4 result contract: it maps REJECT_ALAC to DEFER_STOCK.  New dispatch
 * integrations must call retained_format_router_classify_detailed() so
 * recognized ALAC cannot fall through to a stock AAC decoder.
 * A BRIDGE result authorizes only an attempted retained-bridge open: the
 * container/decoder must still validate sample tables and compressed data,
 * and its failure must remain terminal rather than retrying as another codec.
 * Likewise, STOCK is an observation, not a promise that the stock parser will
 * accept every codec detail; integration treats it exactly like DEFER_STOCK.
 */
retained_format_route retained_format_router_classify(
    const retained_format_router_source *source);

#endif
