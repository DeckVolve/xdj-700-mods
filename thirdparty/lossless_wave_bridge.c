#include "lossless_wave_bridge.h"

#include <limits.h>
#include <stddef.h>

enum {
    LOSSLESS_WAVE_BRIDGE_CLOSED = 0u,
    LOSSLESS_WAVE_BRIDGE_OPENING = 0x574F504Eu, /* "WOPN" */
    LOSSLESS_WAVE_BRIDGE_ACTIVE = 0x57414354u,  /* "WACT" */
    LOSSLESS_WAVE_BRIDGE_ERROR = 0x57455252u   /* "WERR" */
};

static void copy_bytes(unsigned char *destination,
                       const unsigned char *source, unsigned count)
{
    unsigned index;

    for (index = 0u; index < count; index++)
        destination[index] = source[index];
}

static int range_end(uintptr_t start, unsigned count, uintptr_t *end)
{
    if (!end || start > UINTPTR_MAX - count)
        return 0;
    *end = start + count;
    return 1;
}

static int ranges_overlap(const void *left, unsigned left_count,
                          const void *right, unsigned right_count,
                          int *valid)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (valid)
        *valid = 0;
    if (left_count == 0u || right_count == 0u) {
        if (valid)
            *valid = 1;
        return 0;
    }
    if (!left || !right || !range_end(left_start, left_count, &left_end) ||
        !range_end(right_start, right_count, &right_end))
        return 0;
    if (valid)
        *valid = 1;
    return left_start < right_end && right_start < left_end;
}

static int ranges_disjoint(const void *left, unsigned left_count,
                           const void *right, unsigned right_count)
{
    int valid;
    int overlap = ranges_overlap(left, left_count, right, right_count,
                                 &valid);

    return valid && !overlap;
}

static int u64_is_zero(uint64_t value)
{
    return (uint32_t)(value >> 32u) == 0u && (uint32_t)value == 0u;
}

static int u64_greater(uint64_t left, uint64_t right)
{
    uint32_t left_high = (uint32_t)(left >> 32u);
    uint32_t right_high = (uint32_t)(right >> 32u);

    if (left_high != right_high)
        return left_high > right_high;
    return (uint32_t)left > (uint32_t)right;
}

static int range_conflict_status(const void *memory, unsigned capacity,
                                 const void *owned,
                                 unsigned owned_capacity)
{
    int valid;
    int overlap = ranges_overlap(memory, capacity, owned, owned_capacity,
                                 &valid);

    if (!valid)
        return AUDIO_FILE_ERR_RANGE;
    return overlap ? AUDIO_FILE_ERR_ARGUMENT : AUDIO_FILE_OK;
}

static int owned_range_status(
    const lossless_wave_bridge *bridge, const void *memory,
    unsigned capacity)
{
    int result = range_conflict_status(
        memory, capacity, bridge, (unsigned)sizeof(*bridge));

    if (result != AUDIO_FILE_OK)
        return result;
    result = range_conflict_status(memory, capacity, bridge->fifo,
                                   bridge->fifo_capacity);
    if (result != AUDIO_FILE_OK)
        return result;
    if (bridge->info.kind == AUDIO_FILE_KIND_FLAC)
        return range_conflict_status(
            memory, capacity, bridge->storage.flac_arena,
            bridge->storage.flac_arena_capacity);
    if (bridge->info.kind != AUDIO_FILE_KIND_ALAC_ISOBMFF)
        return AUDIO_FILE_ERR_STATE;
    result = range_conflict_status(
        memory, capacity, bridge->storage.alac_workspace,
        bridge->storage.alac_workspace_capacity);
    if (result != AUDIO_FILE_OK)
        return result;
    return range_conflict_status(
        memory, capacity, bridge->storage.alac_packet_storage,
        bridge->storage.alac_packet_capacity);
}

static int capability_valid(const dual_format_payload_info *info)
{
    unsigned frame_bytes;

    if (!info ||
        (info->kind != AUDIO_FILE_KIND_FLAC &&
         info->kind != AUDIO_FILE_KIND_ALAC_ISOBMFF) ||
        (info->sample_rate != 44100u && info->sample_rate != 48000u) ||
        info->channels != LOSSLESS_WAVE_BRIDGE_CHANNELS ||
        (info->bits_per_sample != 16u && info->bits_per_sample != 24u) ||
        u64_is_zero(info->total_frames))
        return 0;
    frame_bytes = info->channels * (info->bits_per_sample / 8u);
    /* The native source-size boundary is 32-bit.  Keep the complete virtual
     * WAVE extent, not merely RIFF's chunk-size field, representable there. */
    return !u64_greater(
        info->total_frames,
        ((uint64_t)UINT32_MAX - LOSSLESS_WAVE_BRIDGE_HEADER_BYTES) /
            frame_bytes);
}

static unsigned required_fifo_bytes(const dual_format_payload_info *info)
{
    unsigned frames;
    unsigned frame_bytes;

    if (!capability_valid(info))
        return 0u;
    frames = info->kind == AUDIO_FILE_KIND_FLAC
        ? FLAC_MOD_MAX_BLOCKSIZE : ALAC_MOD_MAX_FRAME_SAMPLES;
    frame_bytes = info->channels * (info->bits_per_sample / 8u);
    return frames * frame_bytes;
}

static int source_read_exact(const dual_format_payload_source *source,
                             uint64_t offset, unsigned char *destination,
                             unsigned count)
{
    unsigned done = 0u;

    if (!source || !source->read_at ||
        (count != 0u && !destination) || offset > source->size ||
        (uint64_t)count > source->size - offset)
        return AUDIO_FILE_ERR_FORMAT;
    while (done < count) {
        unsigned got = 0u;
        int result = source->read_at(
            source->read_user, offset + done, destination + done,
            count - done, &got);

        if (result != 0 || got == 0u || got > count - done)
            return AUDIO_FILE_ERR_IO;
        done += got;
    }
    return AUDIO_FILE_OK;
}

static int validate_flac_block_range(
    const dual_format_payload_source *source)
{
    unsigned char streaminfo_prefix[12u];
    unsigned minimum;
    unsigned maximum;
    int result = source_read_exact(source, 0u, streaminfo_prefix,
                                   sizeof(streaminfo_prefix));

    if (result != AUDIO_FILE_OK)
        return result;
    if (streaminfo_prefix[0] != 'f' || streaminfo_prefix[1] != 'L' ||
        streaminfo_prefix[2] != 'a' || streaminfo_prefix[3] != 'C' ||
        (streaminfo_prefix[4] & 0x7fu) != 0u ||
        streaminfo_prefix[5] != 0u || streaminfo_prefix[6] != 0u ||
        streaminfo_prefix[7] != 34u)
        return AUDIO_FILE_ERR_FORMAT;
    minimum = ((unsigned)streaminfo_prefix[8] << 8u) |
              streaminfo_prefix[9];
    maximum = ((unsigned)streaminfo_prefix[10] << 8u) |
              streaminfo_prefix[11];
    if (minimum == 0u || minimum > maximum ||
        maximum > FLAC_MOD_MAX_BLOCKSIZE)
        return AUDIO_FILE_ERR_UNSUPPORTED;
    return AUDIO_FILE_OK;
}

static void put_le16(unsigned char *destination, unsigned value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
}

static void put_le32(unsigned char *destination, uint32_t value)
{
    destination[0] = (unsigned char)value;
    destination[1] = (unsigned char)(value >> 8);
    destination[2] = (unsigned char)(value >> 16);
    destination[3] = (unsigned char)(value >> 24);
}

static void build_wave_header(const lossless_wave_bridge *bridge,
                              unsigned char header[
                                  LOSSLESS_WAVE_BRIDGE_HEADER_BYTES])
{
    uint32_t pcm_bytes = (uint32_t)bridge->pcm_bytes;

    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    put_le32(header + 4u, 36u + pcm_bytes);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    put_le32(header + 16u, 16u);
    put_le16(header + 20u, 1u);
    put_le16(header + 22u, bridge->info.channels);
    put_le32(header + 24u, bridge->info.sample_rate);
    put_le32(header + 28u,
             bridge->info.sample_rate * bridge->frame_bytes);
    put_le16(header + 32u, bridge->frame_bytes);
    put_le16(header + 34u, bridge->info.bits_per_sample);
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    put_le32(header + 40u, pcm_bytes);
}

static int bridge_pcm_sink(const int32_t * const channels[],
                           unsigned frame_count, unsigned channel_count,
                           unsigned bits_per_sample, void *user)
{
    lossless_wave_bridge *bridge = (lossless_wave_bridge *)user;
    unsigned byte_count;
    unsigned frame;
    unsigned output = 0u;

    if (!bridge || bridge->state != LOSSLESS_WAVE_BRIDGE_ACTIVE ||
        !bridge->operation_active || bridge->block_bytes != 0u ||
        bridge->pending_frames != 0u || !channels || !channels[0] ||
        !channels[1] || frame_count == 0u ||
        channel_count != bridge->info.channels ||
        bits_per_sample != bridge->info.bits_per_sample)
        return 1;
    if (bridge->frame_bytes == 0u ||
        frame_count > UINT_MAX / bridge->frame_bytes)
        return 1;
    byte_count = frame_count * bridge->frame_bytes;
    if (byte_count > bridge->fifo_capacity ||
        bridge->decoded_bytes > bridge->pcm_bytes ||
        byte_count > bridge->pcm_bytes - bridge->decoded_bytes)
        return 1;

    for (frame = 0u; frame < frame_count; frame++) {
        unsigned channel;

        for (channel = 0u; channel < bridge->info.channels;
             channel++) {
            int32_t sample = channels[channel][frame];

            if (bits_per_sample == 16u) {
                uint16_t packed;

                if (sample < -32768 || sample > 32767)
                    return 1;
                packed = (uint16_t)(int16_t)sample;
                bridge->fifo[output++] = (unsigned char)packed;
                bridge->fifo[output++] = (unsigned char)(packed >> 8);
            } else {
                uint32_t packed;

                if (sample < -8388608 || sample > 8388607)
                    return 1;
                packed = (uint32_t)sample;
                bridge->fifo[output++] = (unsigned char)packed;
                bridge->fifo[output++] = (unsigned char)(packed >> 8);
                bridge->fifo[output++] = (unsigned char)(packed >> 16);
            }
        }
    }
    bridge->block_bytes = byte_count;
    bridge->pending_frames = frame_count;
    return 0;
}

static int payload_error(int result)
{
    int error;

    if (result >= 0)
        return AUDIO_FILE_ERR_DECODER;
    error = -result;
    if (error < AUDIO_FILE_ERR_ARGUMENT || error > AUDIO_FILE_ERR_STATE)
        return AUDIO_FILE_ERR_DECODER;
    return error;
}

static int reject_trailing_pcm(const int32_t * const channels[],
                               unsigned frame_count,
                               unsigned channel_count,
                               unsigned bits_per_sample, void *user)
{
    unsigned *called = (unsigned *)user;

    (void)channels;
    (void)frame_count;
    (void)channel_count;
    (void)bits_per_sample;
    if (called)
        *called = 1u;
    return 1;
}

static int validate_decoder_end(lossless_wave_bridge *bridge)
{
    unsigned frames = 0u;
    unsigned sink_called = 0u;
    int result = dual_format_payload_session_next(
        &bridge->decoder, reject_trailing_pcm, &sink_called, &frames);

    if (result == DUAL_FORMAT_PAYLOAD_NEXT_END && frames == 0u &&
        sink_called == 0u) {
        bridge->decoder_ended = 1u;
        return AUDIO_FILE_OK;
    }
    bridge->state = LOSSLESS_WAVE_BRIDGE_ERROR;
    bridge->block_bytes = 0u;
    bridge->pending_frames = 0u;
    if (sink_called != 0u || result == DUAL_FORMAT_PAYLOAD_NEXT_BLOCK)
        return AUDIO_FILE_ERR_FRAME_COUNT;
    return payload_error(result);
}

static int decode_next_block(lossless_wave_bridge *bridge)
{
    unsigned frames = 0u;
    unsigned expected_bytes;
    int result;

    bridge->block_bytes = 0u;
    bridge->pending_frames = 0u;
    result = dual_format_payload_session_next(
        &bridge->decoder, bridge_pcm_sink, bridge, &frames);
    if (result == DUAL_FORMAT_PAYLOAD_NEXT_BLOCK) {
        if (frames == 0u || frames != bridge->pending_frames ||
            bridge->frame_bytes == 0u ||
            frames > UINT_MAX / bridge->frame_bytes) {
            bridge->state = LOSSLESS_WAVE_BRIDGE_ERROR;
            bridge->block_bytes = 0u;
            bridge->pending_frames = 0u;
            return AUDIO_FILE_ERR_DECODER;
        }
        expected_bytes = frames * bridge->frame_bytes;
        if (bridge->block_bytes != expected_bytes ||
            bridge->decoded_bytes > bridge->pcm_bytes ||
            expected_bytes > bridge->pcm_bytes - bridge->decoded_bytes) {
            bridge->state = LOSSLESS_WAVE_BRIDGE_ERROR;
            bridge->block_bytes = 0u;
            bridge->pending_frames = 0u;
            return AUDIO_FILE_ERR_DECODER;
        }
        bridge->block_base = bridge->decoded_bytes;
        bridge->decoded_bytes += expected_bytes;
        bridge->pending_frames = 0u;
        /* Validate exact terminal framing as soon as the declared final block
         * is retained.  This preserves that block for replay while making a
         * later virtual-EOF probe side-effect-free. */
        if (bridge->decoded_bytes == bridge->pcm_bytes)
            return validate_decoder_end(bridge);
        return AUDIO_FILE_OK;
    }
    bridge->block_bytes = 0u;
    bridge->pending_frames = 0u;
    if (result == DUAL_FORMAT_PAYLOAD_NEXT_END) {
        if (frames != 0u || bridge->decoded_bytes != bridge->pcm_bytes) {
            bridge->state = LOSSLESS_WAVE_BRIDGE_ERROR;
            return AUDIO_FILE_ERR_FRAME_COUNT;
        }
        bridge->decoder_ended = 1u;
        return AUDIO_FILE_OK;
    }
    bridge->state = LOSSLESS_WAVE_BRIDGE_ERROR;
    return payload_error(result);
}

static int payload_info_equal(const dual_format_payload_info *left,
                              const dual_format_payload_info *right)
{
    return left && right && left->kind == right->kind &&
           left->sample_rate == right->sample_rate &&
           left->channels == right->channels &&
           left->bits_per_sample == right->bits_per_sample &&
           left->total_frames == right->total_frames &&
           left->data_offset == right->data_offset;
}

/* Restart the retained decoder without changing any public bridge geometry or
 * allocating another storage plane.  The caller holds operation_active, so
 * source-callback attempts to read, close, or reopen this bridge remain
 * rejected throughout the close/open/validation interval.
 *
 * The decoder and its caller-owned workspace necessarily change while the
 * source is reopened.  All externally observable bridge positioning fields,
 * however, are committed only after the new session has passed the complete
 * initial capability contract again.  A failure is terminal and releases any
 * newly acquired payload ownership; no partly reset ACTIVE bridge is exposed.
 */
static int reopen_decoder(lossless_wave_bridge *bridge)
{
    dual_format_payload_info reopened_info;
    int result;

    dual_format_payload_session_close(&bridge->decoder);
    result = dual_format_payload_session_open(
        &bridge->decoder, &bridge->source, &bridge->storage,
        &reopened_info);
    if (result != AUDIO_FILE_OK)
        goto fail;
    if (!capability_valid(&reopened_info)) {
        result = AUDIO_FILE_ERR_UNSUPPORTED;
        goto fail_session;
    }
    if (u64_greater(reopened_info.total_frames,
                    bridge->storage.max_frames)) {
        result = AUDIO_FILE_ERR_FRAME_LIMIT;
        goto fail_session;
    }
    if (reopened_info.kind == AUDIO_FILE_KIND_FLAC) {
        result = validate_flac_block_range(&bridge->source);
        if (result != AUDIO_FILE_OK)
            goto fail_session;
    }
    /* Reopening the same stable source must reproduce every field that
     * determines the virtual WAVE view and decoder payload boundary. */
    if (!payload_info_equal(&reopened_info, &bridge->info)) {
        result = AUDIO_FILE_ERR_FORMAT;
        goto fail_session;
    }

    bridge->block_base = 0u;
    bridge->decoded_bytes = 0u;
    bridge->block_bytes = 0u;
    bridge->pending_frames = 0u;
    bridge->decoder_ended = 0u;
    return AUDIO_FILE_OK;

fail_session:
    dual_format_payload_session_close(&bridge->decoder);
fail:
    bridge->state = LOSSLESS_WAVE_BRIDGE_ERROR;
    bridge->block_base = 0u;
    bridge->decoded_bytes = 0u;
    bridge->block_bytes = 0u;
    bridge->pending_frames = 0u;
    bridge->decoder_ended = 0u;
    return result;
}

static int position_pcm(lossless_wave_bridge *bridge, uint32_t position)
{
    int result;

    if (position > bridge->pcm_bytes)
        return AUDIO_FILE_ERR_RANGE;
    for (;;) {
        if (bridge->block_bytes != 0u) {
            uint32_t block_end =
                bridge->block_base + bridge->block_bytes;

            if (position < bridge->block_base)
                return AUDIO_FILE_ERR_STATE;
            if (position < block_end)
                return AUDIO_FILE_OK;
            bridge->block_bytes = 0u;
        }
        if (bridge->decoder_ended)
            return position == bridge->pcm_bytes ? AUDIO_FILE_OK
                                                 : AUDIO_FILE_ERR_STATE;
        result = decode_next_block(bridge);
        if (result != AUDIO_FILE_OK)
            return result;
    }
}

int lossless_wave_bridge_open(
    lossless_wave_bridge *bridge,
    const dual_format_payload_source *source,
    const dual_format_payload_storage *storage,
    unsigned char *fifo,
    unsigned fifo_capacity,
    dual_format_payload_info *info_out)
{
    dual_format_payload_info opened_info;
    int result;

    if (!bridge) {
        if (info_out)
            *info_out = (dual_format_payload_info){0};
        return AUDIO_FILE_ERR_ARGUMENT;
    }
    /* The caller must supply a zero-initialized persistent object.  Most
     * importantly, reject a duplicate or callback-reentrant open before any
     * output or live decoder state is modified. */
    if (bridge->state != LOSSLESS_WAVE_BRIDGE_CLOSED ||
        bridge->operation_active != 0u)
        return AUDIO_FILE_ERR_STATE;
    if (info_out &&
        !ranges_disjoint(info_out, (unsigned)sizeof(*info_out), bridge,
                         (unsigned)sizeof(*bridge)))
        return AUDIO_FILE_ERR_ARGUMENT;
    if (!source || !storage || !fifo ||
        fifo_capacity < LOSSLESS_WAVE_BRIDGE_MIN_FIFO_BYTES) {
        if (info_out)
            *info_out = (dual_format_payload_info){0};
        return AUDIO_FILE_ERR_ARGUMENT;
    }
    if (!ranges_disjoint(source, (unsigned)sizeof(*source), bridge,
                         (unsigned)sizeof(*bridge)) ||
        !ranges_disjoint(storage, (unsigned)sizeof(*storage), bridge,
                         (unsigned)sizeof(*bridge)))
        return AUDIO_FILE_ERR_ARGUMENT;
    if (info_out &&
        (!ranges_disjoint(info_out, (unsigned)sizeof(*info_out), source,
                          (unsigned)sizeof(*source)) ||
         !ranges_disjoint(info_out, (unsigned)sizeof(*info_out), storage,
                          (unsigned)sizeof(*storage))))
        return AUDIO_FILE_ERR_ARGUMENT;
    if (!source->read_at || u64_is_zero(storage->max_frames)) {
        if (info_out)
            *info_out = (dual_format_payload_info){0};
        return AUDIO_FILE_ERR_ARGUMENT;
    }
    if (!ranges_disjoint(fifo, fifo_capacity, bridge,
                         (unsigned)sizeof(*bridge)) ||
        !ranges_disjoint(storage->flac_arena,
                         storage->flac_arena_capacity, bridge,
                         (unsigned)sizeof(*bridge)) ||
        !ranges_disjoint(storage->alac_workspace,
                         storage->alac_workspace_capacity, bridge,
                         (unsigned)sizeof(*bridge)) ||
        !ranges_disjoint(storage->alac_packet_storage,
                         storage->alac_packet_capacity, bridge,
                         (unsigned)sizeof(*bridge)))
        return AUDIO_FILE_ERR_WORKSPACE;
    /* These checks are format-independent so no source callback is needed to
     * decide which workspace is live.  FLAC's arena may intentionally alias
     * both ALAC views, but the FIFO must alias none of them and the two ALAC
     * views must remain disjoint. */
    if (!ranges_disjoint(fifo, fifo_capacity, storage->flac_arena,
                         storage->flac_arena_capacity) ||
        !ranges_disjoint(fifo, fifo_capacity, storage->alac_workspace,
                         storage->alac_workspace_capacity) ||
        !ranges_disjoint(fifo, fifo_capacity,
                         storage->alac_packet_storage,
                         storage->alac_packet_capacity) ||
        !ranges_disjoint(storage->alac_workspace,
                         storage->alac_workspace_capacity,
                         storage->alac_packet_storage,
                         storage->alac_packet_capacity))
        return AUDIO_FILE_ERR_WORKSPACE;
    if (info_out &&
        (!ranges_disjoint(info_out, (unsigned)sizeof(*info_out), fifo,
                          fifo_capacity) ||
         !ranges_disjoint(info_out, (unsigned)sizeof(*info_out),
                          storage->flac_arena,
                          storage->flac_arena_capacity) ||
         !ranges_disjoint(info_out, (unsigned)sizeof(*info_out),
                          storage->alac_workspace,
                          storage->alac_workspace_capacity) ||
         !ranges_disjoint(info_out, (unsigned)sizeof(*info_out),
                          storage->alac_packet_storage,
                          storage->alac_packet_capacity)))
        return AUDIO_FILE_ERR_ARGUMENT;

    if (info_out)
        *info_out = (dual_format_payload_info){0};
    *bridge = (lossless_wave_bridge){0};

    bridge->source = *source;
    bridge->storage = *storage;
    bridge->fifo = fifo;
    bridge->fifo_capacity = fifo_capacity;
    bridge->state = LOSSLESS_WAVE_BRIDGE_OPENING;
    bridge->operation_active = 1u;

    /* session_open acquires the payload's process-wide owner before its first
     * source callback.  It must be the first callback-capable operation here:
     * a callback-triggered open on any other bridge then returns STATE without
     * reading the nested source. */
    result = dual_format_payload_session_open(
        &bridge->decoder, &bridge->source, &bridge->storage, &opened_info);
    if (result != AUDIO_FILE_OK)
        goto fail_closed;
    bridge->info = opened_info;
    if (!capability_valid(&bridge->info)) {
        result = AUDIO_FILE_ERR_UNSUPPORTED;
        goto fail_session;
    }
    if (bridge->fifo_capacity < required_fifo_bytes(&bridge->info)) {
        result = AUDIO_FILE_ERR_WORKSPACE;
        goto fail_session;
    }
    if (u64_greater(bridge->info.total_frames,
                    bridge->storage.max_frames)) {
        result = AUDIO_FILE_ERR_FRAME_LIMIT;
        goto fail_session;
    }
    if (bridge->info.kind == AUDIO_FILE_KIND_FLAC) {
        result = validate_flac_block_range(&bridge->source);
        if (result != AUDIO_FILE_OK)
            goto fail_session;
    }

    bridge->storage.max_frames = bridge->info.total_frames;
    bridge->frame_bytes = bridge->info.channels *
        (bridge->info.bits_per_sample / 8u);
    bridge->pcm_bytes = (uint32_t)bridge->info.total_frames *
        bridge->frame_bytes;
    bridge->wave_bytes = LOSSLESS_WAVE_BRIDGE_HEADER_BYTES +
        bridge->pcm_bytes;
    bridge->state = LOSSLESS_WAVE_BRIDGE_ACTIVE;
    /* Keep close/read reentrancy blocked through every source callback made
     * by session_open.  The bridge becomes externally usable only here. */
    bridge->operation_active = 0u;
    if (info_out)
        *info_out = bridge->info;
    return AUDIO_FILE_OK;

fail_session:
    dual_format_payload_session_close(&bridge->decoder);
fail_closed:
    *bridge = (lossless_wave_bridge){0};
    return result;
}

uint64_t lossless_wave_bridge_size(const lossless_wave_bridge *bridge)
{
    if (!bridge || bridge->state != LOSSLESS_WAVE_BRIDGE_ACTIVE)
        return 0u;
    return bridge->wave_bytes;
}

uint64_t lossless_wave_bridge_replay_floor(
    const lossless_wave_bridge *bridge)
{
    uint32_t discarded_pcm;

    if (lossless_wave_bridge_size(bridge) == 0u)
        return 0u;
    discarded_pcm = bridge->block_bytes != 0u ? bridge->block_base :
                    bridge->decoded_bytes;
    if (discarded_pcm == 0u)
        return 0u;
    return (uint64_t)LOSSLESS_WAVE_BRIDGE_HEADER_BYTES + discarded_pcm;
}

int lossless_wave_bridge_can_replay_at(const lossless_wave_bridge *bridge,
                                       uint64_t offset)
{
    uint64_t wave_bytes;
    uint64_t replay_start;
    uint64_t replay_end;

    wave_bytes = lossless_wave_bridge_size(bridge);
    if (wave_bytes == 0u || offset > wave_bytes)
        return 0;
    if (offset == wave_bytes)
        return 1;
    if (!bridge || bridge->block_bytes == 0u)
        return 0;

    replay_start = (uint64_t)LOSSLESS_WAVE_BRIDGE_HEADER_BYTES +
                   bridge->block_base;
    replay_end = replay_start + bridge->block_bytes;
    if (offset >= replay_start && offset < replay_end)
        return 1;

    /* Header-only reads are random-access, but seeking back to the header
     * after the first PCM block was discarded cannot restart later PCM. */
    return offset < LOSSLESS_WAVE_BRIDGE_HEADER_BYTES &&
           bridge->block_base == 0u;
}

int lossless_wave_bridge_read_at(
    lossless_wave_bridge *bridge,
    uint64_t offset,
    void *destination,
    unsigned count,
    unsigned *bytes_read)
{
    unsigned char header[LOSSLESS_WAVE_BRIDGE_HEADER_BYTES];
    unsigned char *output = (unsigned char *)destination;
    uint32_t offset32;
    uint32_t remaining;
    unsigned wanted;
    unsigned copied = 0u;
    int result;

    if (!bytes_read)
        return AUDIO_FILE_ERR_ARGUMENT;
    if (!bridge) {
        *bytes_read = 0u;
        return AUDIO_FILE_ERR_ARGUMENT;
    }
    /* Do not clear an aliased result pointer: doing so would corrupt the very
     * state needed to reject the call. */
    result = range_conflict_status(bytes_read, (unsigned)sizeof(*bytes_read),
                                   bridge, (unsigned)sizeof(*bridge));
    if (result != AUDIO_FILE_OK)
        return result;
    if (bridge->state != LOSSLESS_WAVE_BRIDGE_ACTIVE ||
        bridge->operation_active) {
        *bytes_read = 0u;
        return AUDIO_FILE_ERR_STATE;
    }
    result = owned_range_status(bridge, bytes_read,
                                (unsigned)sizeof(*bytes_read));
    if (result != AUDIO_FILE_OK)
        return result;
    if (count != 0u && !destination) {
        *bytes_read = 0u;
        return AUDIO_FILE_ERR_ARGUMENT;
    }
    if (u64_greater(offset, (uint64_t)bridge->wave_bytes)) {
        *bytes_read = 0u;
        return AUDIO_FILE_ERR_RANGE;
    }
    offset32 = (uint32_t)offset;
    remaining = bridge->wave_bytes - offset32;
    wanted = count < remaining ? count : remaining;
    if (count == 0u) {
        *bytes_read = 0u;
        return AUDIO_FILE_OK;
    }
    /* EOF probes are metadata operations.  They must not decode an entire
     * stream or discard the replay window.  Exact terminal framing is checked
     * when the declared final PCM block is decoded. */
    if (wanted == 0u) {
        *bytes_read = 0u;
        return AUDIO_FILE_OK;
    }
    result = owned_range_status(bridge, destination, wanted);
    if (result != AUDIO_FILE_OK) {
        *bytes_read = 0u;
        return result;
    }
    result = range_conflict_status(
        destination, wanted, bytes_read, (unsigned)sizeof(*bytes_read));
    if (result != AUDIO_FILE_OK)
        return result;
    *bytes_read = 0u;

    bridge->operation_active = 1u;
    /* Bytes below the retained replay floor require a decoder restart.  Do it
     * before building or publishing even a header-only result: a cue-style
     * header read followed by PCM then observes one coherent decoder-at-zero
     * session.  Exact EOF and zero-byte metadata probes returned above without
     * disturbing the retained decoder. */
    if ((uint64_t)offset32 < lossless_wave_bridge_replay_floor(bridge)) {
        result = reopen_decoder(bridge);
        if (result != AUDIO_FILE_OK) {
            bridge->operation_active = 0u;
            return result;
        }
    }
    /* For a header-to-PCM read, establish the PCM position before publishing
     * any header byte.  Thus a decoder/forward-decode failure remains atomic. */
    if (offset32 < LOSSLESS_WAVE_BRIDGE_HEADER_BYTES &&
        wanted > LOSSLESS_WAVE_BRIDGE_HEADER_BYTES - offset32) {
        result = position_pcm(bridge, 0u);
        if (result != AUDIO_FILE_OK) {
            bridge->operation_active = 0u;
            return result;
        }
    }

    if (offset32 < LOSSLESS_WAVE_BRIDGE_HEADER_BYTES) {
        unsigned header_count =
            LOSSLESS_WAVE_BRIDGE_HEADER_BYTES - offset32;

        if (header_count > wanted)
            header_count = wanted;
        build_wave_header(bridge, header);
        copy_bytes(output, header + offset32, header_count);
        copied = header_count;
        offset32 += header_count;
        if (copied == wanted) {
            bridge->operation_active = 0u;
            *bytes_read = copied;
            return AUDIO_FILE_OK;
        }
    }

    {
        uint32_t pcm_position =
            offset32 - LOSSLESS_WAVE_BRIDGE_HEADER_BYTES;
        uint32_t block_offset;
        unsigned available;
        unsigned request = wanted - copied;

        result = position_pcm(bridge, pcm_position);
        if (result != AUDIO_FILE_OK) {
            bridge->operation_active = 0u;
            return result;
        }
        if (bridge->block_bytes == 0u) {
            bridge->operation_active = 0u;
            *bytes_read = copied;
            return AUDIO_FILE_OK;
        }
        block_offset = pcm_position - bridge->block_base;
        available = bridge->block_bytes - block_offset;
        if (request > available)
            request = available;
        copy_bytes(output + copied,
                   bridge->fifo + block_offset, request);
        copied += request;
    }

    bridge->operation_active = 0u;
    *bytes_read = copied;
    return AUDIO_FILE_OK;
}

void lossless_wave_bridge_close(lossless_wave_bridge *bridge)
{
    if (!bridge || bridge->operation_active)
        return;
    dual_format_payload_session_close(&bridge->decoder);
    *bridge = (lossless_wave_bridge){0};
}
