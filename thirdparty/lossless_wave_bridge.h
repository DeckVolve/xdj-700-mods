#ifndef XDJ700_LOSSLESS_WAVE_BRIDGE_H
#define XDJ700_LOSSLESS_WAVE_BRIDGE_H

/*
 * Retained clean-room FLAC/ALAC-to-PCM-WAVE bridge.
 *
 * This module is public-format code.  It owns no firmware object and knows no
 * XDJ address, mailbox, request, or hardware sample ABI.  A caller supplies a
 * stable random-access compressed source, the dual-format decoder's bounded
 * storage, and one PCM block buffer.  The bridge exposes a canonical PCM WAVE
 * byte view which an integration adapter may feed to an existing WAV reader.
 *
 * PCM reads are forward-streaming with a one-decoder-block fast replay window.
 * PCM bytes in the current retained block may be reread without restarting.
 * A read below the retained replay floor safely closes and reopens the same
 * decoder over the retained source/storage, revalidates the original format
 * contract, and decodes forward to the requested byte.  This provides bounded-
 * storage rewind and cue-to-start semantics; it does not claim reverse-play or
 * low-latency arbitrary hot-cue seeking.
 *
 * A bridge object is persistent private state: initialize it to all zeroes,
 * keep it at one stable address through close, and never copy it while open.
 * Every API entry and every source callback must run in one task or under one
 * external lock.  The payload's process-wide owner is acquired before the
 * first open-time source callback, so a same-task callback-triggered open on
 * any bridge is rejected without reading the nested source.  The owner and
 * object guards are deliberately non-atomic and do not serialize preemption.
 */

#include <stdint.h>

#include "dual_format_payload.h"

#define LOSSLESS_WAVE_BRIDGE_HEADER_BYTES 44u
#define LOSSLESS_WAVE_BRIDGE_CHANNELS 2u
/* Retained baseline constants used by the 44.1-kHz/16-bit fixture suites.
 * An open bridge derives its actual geometry from validated decoder info. */
#define LOSSLESS_WAVE_BRIDGE_BITS_PER_SAMPLE 16u
#define LOSSLESS_WAVE_BRIDGE_SAMPLE_RATE 44100u
#define LOSSLESS_WAVE_BRIDGE_FRAME_BYTES 4u
#define LOSSLESS_WAVE_BRIDGE_MAX_FRAME_BYTES 6u
#define LOSSLESS_WAVE_BRIDGE_MIN_FIFO_BYTES \
    (FLAC_MOD_MAX_BLOCKSIZE * LOSSLESS_WAVE_BRIDGE_FRAME_BYTES)
#define LOSSLESS_WAVE_BRIDGE_MAX_FIFO_BYTES \
    (FLAC_MOD_MAX_BLOCKSIZE * LOSSLESS_WAVE_BRIDGE_MAX_FRAME_BYTES)

typedef struct {
    dual_format_payload_session decoder;
    dual_format_payload_source source;
    dual_format_payload_storage storage;
    dual_format_payload_info info;
    unsigned char *fifo;
    unsigned fifo_capacity;
    uint32_t pcm_bytes;
    uint32_t wave_bytes;
    uint32_t block_base;
    uint32_t decoded_bytes;
    unsigned block_bytes;
    unsigned pending_frames;
    unsigned frame_bytes;
    unsigned state;
    unsigned operation_active;
    unsigned decoder_ended;
} lossless_wave_bridge;

/* Persistent object budget for the audited 32-bit SH-4 build.  This guards
 * object growth only; it is not a task-stack or cumulative call-chain proof. */
#define LOSSLESS_WAVE_BRIDGE_SH4_OBJECT_MAX_BYTES 768u
#if UINTPTR_MAX == UINT32_MAX
typedef char lossless_wave_bridge_sh4_object_size_must_fit[
    sizeof(lossless_wave_bridge) <=
        LOSSLESS_WAVE_BRIDGE_SH4_OBJECT_MAX_BYTES ? 1 : -1];
#endif

/*
 * Probe and open one source, restricted to the development envelope:
 * native FLAC or self-contained ISO-BMFF ALAC, 44.1/48 kHz, stereo, signed
 * 16/24-bit, and a known nonzero frame count representable by canonical RIFF.
 *
 * bridge must be all-zero initialized.  Calling open again before close is
 * rejected without modifying the live session.  A callback-triggered open on
 * a different bridge is rejected before any callback reads that nested source.
 * source callbacks/user state and every storage byte must remain valid until
 * close.  storage->max_frames is a caller policy cap; after the guarded format
 * open, the bridge tightens its retained copy to the exact declared frame
 * count before any decode call.  FLAC STREAMINFO maximum block size is
 * rejected during this open when it exceeds FLAC_MOD_MAX_BLOCKSIZE.  fifo must
 * not overlap the bridge or any supplied decoder-storage view and must hold at
 * least LOSSLESS_WAVE_BRIDGE_MIN_FIFO_BYTES. After source metadata has been
 * validated, 24-bit FLAC requires LOSSLESS_WAVE_BRIDGE_MAX_FIFO_BYTES and
 * 24-bit ALAC requires ALAC_MOD_MAX_FRAME_SAMPLES * 6 bytes; insufficient
 * format-specific capacity fails open with AUDIO_FILE_ERR_WORKSPACE. The
 * bridge, descriptors,
 * info_out, FIFO, decoder storage, callback user state, and compressed-source
 * backing must be mutually disjoint wherever one can be written while the
 * session is active.  "Disjoint" includes physical memory: cached/uncached or
 * MMU aliases of the same bytes are forbidden even when their uintptr_t ranges
 * differ.  This API can check only virtual ranges; opaque callback/user/backing
 * ranges and physical aliases remain the caller's responsibility.  info_out is
 * optional.
 */
int lossless_wave_bridge_open(
    lossless_wave_bridge *bridge,
    const dual_format_payload_source *source,
    const dual_format_payload_storage *storage,
    unsigned char *fifo,
    unsigned fifo_capacity,
    dual_format_payload_info *info_out);

/* Total virtual RIFF/WAVE byte size after a successful open, otherwise zero. */
uint64_t lossless_wave_bridge_size(const lossless_wave_bridge *bridge);

/* Return the first virtual-WAVE byte retained by the current decoder session.
 * Zero means no decoded PCM prefix has been discarded; a nonzero result
 * includes the 44-byte virtual header.  Reads below this floor trigger a
 * guarded decoder reopen and forward decode rather than using retained PCM. */
uint64_t lossless_wave_bridge_replay_floor(
    const lossless_wave_bridge *bridge);

/* Return nonzero when a virtual-WAVE position can be served from the current
 * bridge state without restarting the decoder.  The current retained PCM
 * block and the header are fast-replayable only while the first PCM block
 * remains retained; exact virtual EOF is always safe.  A zero result for an
 * in-range offset does not mean read_at will fail: read_at can reopen and
 * decode forward from zero. */
int lossless_wave_bridge_can_replay_at(const lossless_wave_bridge *bridge,
                                       uint64_t offset);

/*
 * Read at most count bytes from the virtual WAVE view.  Successful short reads
 * are deliberate at decoder-block boundaries; exact-read callers must retry
 * with the advanced offset.  A nonzero return is an AUDIO_FILE_ERR_* value
 * and clears bytes_read when bytes_read itself is a safe external range.
 * destination and bytes_read must not overlap each other, the bridge, FIFO,
 * or active decoder storage.  Source callback/user/backing aliasing remains
 * the caller's responsibility.  No destination byte is modified unless the
 * read succeeds.  A read at exact virtual EOF returns zero without decoding
 * or discarding the retained replay block.
 *
 * A backward request below replay_floor closes/reopens the same decoder using
 * the retained source and storage, revalidates capability, frame limits, exact
 * metadata, and the FLAC block-size bound, then decodes forward to the target.
 * No storage is allocated.  A source, decoder, reopen-validation, framing, or
 * sink-protocol failure is terminal: the virtual size becomes zero and all
 * later reads fail with AUDIO_FILE_ERR_STATE.  The failed call leaves
 * destination bytes unchanged and never exposes a partially reset ACTIVE
 * bridge.  The caller must invoke close even after a terminal read error to
 * release process-wide decoder ownership; only then may it open a bridge.
 */
int lossless_wave_bridge_read_at(
    lossless_wave_bridge *bridge,
    uint64_t offset,
    void *destination,
    unsigned count,
    unsigned *bytes_read);

/* Cancel/release decoder ownership, including after a terminal read failure.
 * Safe to call repeatedly.  A reentrant close from a source callback is
 * ignored; the owning outer call retains the session and its caller must close
 * after that call returns. */
void lossless_wave_bridge_close(lossless_wave_bridge *bridge);

#endif
