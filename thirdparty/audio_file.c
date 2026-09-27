#include "audio_file.h"

#include <limits.h>
#include <stdint.h>

#include "flac_metadata.h"
#include "flac_mod.h"

#if defined(XDJ700_TARGET_RUNTIME_PROVIDER) && XDJ700_TARGET_RUNTIME_PROVIDER
#include "dual_format_runtime.h"
#endif

typedef struct {
    audio_file_pcm_sink sink;
    void *sink_user;
    uint64_t frames;
    uint64_t max_frames;
    unsigned frame_limit;
    unsigned sink_error;
} audio_sink_context;

enum {
    AUDIO_FILE_STATE_CLOSED = 0,
    AUDIO_FILE_STATE_FLAC_READY = 1,
    AUDIO_FILE_STATE_FLAC_CONSUMED = 2,
    AUDIO_FILE_STATE_ALAC = 3
};

/* Protect destination identity before an open can clear it. The guard rejects
 * recursive bridge entry; external serialization remains the caller's
 * responsibility for true concurrency.  The target build keeps this word in
 * its caller-allocated runtime so the relocatable payload needs no writable
 * global address. */
#if defined(XDJ700_TARGET_RUNTIME_PROVIDER) && XDJ700_TARGET_RUNTIME_PROVIDER
static audio_file *audio_operation_get(void)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();

    return runtime != 0
        ? (audio_file *)runtime->audio_file_active_operation
        : 0;
}

static int audio_operation_acquire(audio_file *file)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();

    if (runtime == 0 || runtime->audio_file_active_operation != 0)
        return 0;
    runtime->audio_file_active_operation = file;
    return 1;
}

static void audio_operation_release(audio_file *file)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();

    if (runtime != 0 && runtime->audio_file_active_operation == file)
        runtime->audio_file_active_operation = 0;
}
#else
static uintptr_t active_audio_file_operation;

static audio_file *audio_operation_get(void)
{
    return (audio_file *)active_audio_file_operation;
}

static int audio_operation_acquire(audio_file *file)
{
    if (active_audio_file_operation != 0)
        return 0;
    active_audio_file_operation = (uintptr_t)file;
    return 1;
}

static void audio_operation_release(audio_file *file)
{
    if (active_audio_file_operation == (uintptr_t)file)
        active_audio_file_operation = 0;
}
#endif

static int audio_ranges_overlap(const void *left, unsigned left_count,
                                const void *right, unsigned right_count)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;

    if (!left || !right || left_count == 0u || right_count == 0u ||
        left_start > UINTPTR_MAX - left_count ||
        right_start > UINTPTR_MAX - right_count)
        return 0;
    return left_start < right_start + right_count &&
           right_start < left_start + left_count;
}

static int audio_range_valid(const void *pointer, unsigned count)
{
    uintptr_t start = (uintptr_t)pointer;

    return pointer && count != 0u && start <= UINTPTR_MAX - count;
}

static int audio_output_conflicts(const audio_file *file,
                                  const void *output, unsigned count)
{
    if (!file)
        return 0;
    if (audio_ranges_overlap(output, count, file,
                             (unsigned)sizeof(*file)))
        return 1;
#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    if (file->state != AUDIO_FILE_STATE_ALAC)
        return 0;
    return audio_ranges_overlap(output, count,
                                file->decoder.alac.packet_storage,
                                file->decoder.alac.packet_capacity) ||
           audio_ranges_overlap(output, count,
                                file->decoder.alac.workspace,
                                file->decoder.alac.workspace_capacity);
#else
    return 0;
#endif
}

enum {
    AUDIO_SOURCE_READ_OK = 0,
    AUDIO_SOURCE_READ_RANGE = 1,
    AUDIO_SOURCE_READ_IO = 2
};

static int source_valid(const audio_file_source *source)
{
    return source && source->read_at;
}

static int source_read_exact(const audio_file_source *source, uint64_t offset,
                             void *destination, unsigned count)
{
    unsigned char *out = (unsigned char *)destination;
    unsigned done = 0u;

    if (!source_valid(source) || (count != 0u && !destination) ||
        offset > source->size || (uint64_t)count > source->size - offset)
        return AUDIO_SOURCE_READ_RANGE;
    while (done < count) {
        unsigned got = 0u;
        int result = source->read_at(source->user, offset + done,
                                     out + done, count - done, &got);

        if (result != 0)
            return AUDIO_SOURCE_READ_IO;
        if (got == 0u || got > count - done)
            return AUDIO_SOURCE_READ_IO;
        done += got;
    }
    return AUDIO_SOURCE_READ_OK;
}

static int read_magic(const audio_file_source *source,
                      unsigned char magic[4])
{
    return source_read_exact(source, 0u, magic, 4u);
}

static int map_magic_read_result(int result)
{
    return result == AUDIO_SOURCE_READ_IO ? AUDIO_FILE_ERR_IO
                                           : AUDIO_FILE_ERR_FORMAT;
}

static int flac_metadata_read_exact(void *user, void *destination,
                                    unsigned offset, unsigned count,
                                    unsigned *bytes_read)
{
    audio_file_source *source = (audio_file_source *)user;

    if (!bytes_read || source_read_exact(source, offset, destination, count) !=
                           AUDIO_SOURCE_READ_OK) {
        if (bytes_read)
            *bytes_read = 0u;
        return 1;
    }
    *bytes_read = count;
    return 0;
}

static int flac_source_read_at(void *user, unsigned offset, void *destination,
                               unsigned count, unsigned *bytes_read)
{
    audio_file_source *source = (audio_file_source *)user;

    if (!bytes_read || !source_valid(source))
        return 1;
    return source->read_at(source->user, (uint64_t)offset, destination,
                           count, bytes_read);
}

static int fill_flac_info(audio_file_source *source, audio_file_info *info)
{
    flac_metadata_info metadata;
    unsigned char frame_sync[2];
    int result;

    if (source->size > UINT_MAX)
        return AUDIO_FILE_ERR_RANGE;
    result = flac_metadata_parse(flac_metadata_read_exact, source, &metadata);
    if (result != FLAC_METADATA_OK) {
        switch (result) {
        case FLAC_METADATA_ERR_ARGUMENT:
            return AUDIO_FILE_ERR_ARGUMENT;
        case FLAC_METADATA_ERR_IO:
            return AUDIO_FILE_ERR_IO;
        case FLAC_METADATA_ERR_UNSUPPORTED:
            return AUDIO_FILE_ERR_UNSUPPORTED;
        case FLAC_METADATA_ERR_OVERFLOW:
            return AUDIO_FILE_ERR_RANGE;
        default:
            return AUDIO_FILE_ERR_FORMAT;
        }
    }
    /* A metadata-only source, or one with a single trailing byte, cannot
     * contain even the two-byte prefix of FLAC's required first audio frame.
     * Reject it before publishing a playable file/WAVE bridge.  A zero
     * STREAMINFO total_samples means unknown, not empty, and is unaffected. */
    if (metadata.frame_data_offset > source->size ||
        source->size - metadata.frame_data_offset < 2u)
        return AUDIO_FILE_ERR_FORMAT;
    result = source_read_exact(source, metadata.frame_data_offset,
                               frame_sync, sizeof(frame_sync));
    if (result != AUDIO_SOURCE_READ_OK)
        return map_magic_read_result(result);
    /* Native FLAC's frame sync is 0xff followed by 0xf8 or 0xf9.  A valid
     * STREAMINFO block followed by arbitrary bytes must not enter the
     * browser/player path as a playable first frame.  Decode still checks
     * the complete header, CRC, and frame payload. */
    if (frame_sync[0] != 0xffu || (frame_sync[1] & 0xfeu) != 0xf8u)
        return AUDIO_FILE_ERR_FORMAT;
    info->kind = AUDIO_FILE_KIND_FLAC;
    info->sample_rate = metadata.sample_rate;
    info->channels = metadata.channels;
    info->bits_per_sample = metadata.bits_per_sample;
    info->total_frames = metadata.total_samples;
    info->data_offset = metadata.frame_data_offset;
    return AUDIO_FILE_OK;
}

#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
static int alac_source_read_exact(void *user, uint64_t offset,
                                  void *destination, unsigned count)
{
    audio_file_source *source = (audio_file_source *)user;

    if (!source_valid(source))
        return 1;
    return source_read_exact(source, offset, destination, count);
}

static void setup_alac_source(audio_file *file,
                              alac_container_source *alac_source)
{
    alac_source->read_at = alac_source_read_exact;
    alac_source->user = &file->source;
    alac_source->size = file->source.size;
}

static void fill_alac_info(const alac_container *container,
                           audio_file_info *info)
{
    info->kind = container->kind == ALAC_CONTAINER_KIND_CAF
                     ? AUDIO_FILE_KIND_ALAC_CAF
                     : AUDIO_FILE_KIND_ALAC_ISOBMFF;
    info->sample_rate = container->config.sample_rate;
    info->channels = container->config.channels;
    info->bits_per_sample = container->config.bit_depth;
    info->total_frames = container->total_frames;
    info->data_offset = 0u;
}

static int map_container_result(int result)
{
    switch (result) {
    case ALAC_CONTAINER_ERR_ARGUMENT:
        return AUDIO_FILE_ERR_ARGUMENT;
    case ALAC_CONTAINER_ERR_IO:
        return AUDIO_FILE_ERR_IO;
    case ALAC_CONTAINER_ERR_UNSUPPORTED:
        return AUDIO_FILE_ERR_UNSUPPORTED;
    case ALAC_CONTAINER_ERR_OVERFLOW:
        return AUDIO_FILE_ERR_RANGE;
    default:
        return AUDIO_FILE_ERR_FORMAT;
    }
}

static int map_stream_result(int result)
{
    switch (result) {
    case ALAC_STREAM_ERR_ARGUMENT:
        return AUDIO_FILE_ERR_ARGUMENT;
    case ALAC_STREAM_ERR_IO:
        return AUDIO_FILE_ERR_IO;
    case ALAC_STREAM_ERR_UNSUPPORTED:
        return AUDIO_FILE_ERR_UNSUPPORTED;
    case ALAC_STREAM_ERR_WORKSPACE:
        return AUDIO_FILE_ERR_WORKSPACE;
    case ALAC_STREAM_ERR_FRAME_LIMIT:
        return AUDIO_FILE_ERR_FRAME_LIMIT;
    case ALAC_STREAM_ERR_FRAME_COUNT:
        return AUDIO_FILE_ERR_FRAME_COUNT;
    case ALAC_STREAM_ERR_OUTPUT:
        return AUDIO_FILE_ERR_OUTPUT;
    case ALAC_STREAM_ERR_RANGE:
        return AUDIO_FILE_ERR_RANGE;
    case ALAC_STREAM_ERR_STATE:
        return AUDIO_FILE_ERR_STATE;
    case ALAC_STREAM_ERR_PACKET:
    case ALAC_STREAM_ERR_CONTAINER:
    default:
        return AUDIO_FILE_ERR_FORMAT;
    }
}
#endif

static int probe_source(audio_file *file, audio_file_info *info)
{
    unsigned char magic[4];
    int result;

    result = read_magic(&file->source, magic);
    if (result != AUDIO_SOURCE_READ_OK)
        return map_magic_read_result(result);
    if (magic[0] == 'f' && magic[1] == 'L' && magic[2] == 'a' &&
        magic[3] == 'C')
        return fill_flac_info(&file->source, info);

#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    {
        alac_container_source alac_source;
        alac_container container;

        setup_alac_source(file, &alac_source);
        result = alac_container_open(&alac_source, &container);
        if (result != ALAC_CONTAINER_OK)
            return map_container_result(result);
        fill_alac_info(&container, info);
    }
    return AUDIO_FILE_OK;
#else
    return AUDIO_FILE_ERR_FORMAT;
#endif
}

int audio_file_probe(const audio_file_source *source, audio_file_info *info)
{
    audio_file file;
    int result;

    /* Probe shares the public bridge's process-wide callback boundary.  Test
     * ownership before clearing info so a source or PCM callback cannot use
     * a nested probe to overwrite live decoder state through its output. */
    if (audio_operation_get() != 0)
        return AUDIO_FILE_ERR_STATE;
    if (!source_valid(source) || !info)
        return AUDIO_FILE_ERR_ARGUMENT;
    if (audio_ranges_overlap(info, (unsigned)sizeof(*info), source,
                             (unsigned)sizeof(*source)))
        return AUDIO_FILE_ERR_ARGUMENT;
    file = (audio_file){0};
    file.source = *source;
    *info = (audio_file_info){0};
    if (!audio_operation_acquire(&file))
        return AUDIO_FILE_ERR_STATE;
    result = probe_source(&file, info);
    audio_operation_release(&file);
    return result;
}

int audio_file_open(audio_file *file, const audio_file_source *source,
                    unsigned char *packet_storage, unsigned packet_capacity,
                    void *workspace, unsigned workspace_capacity,
                    uint64_t max_frames)
{
    unsigned char magic[4];
#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    alac_container_source alac_source;
#endif
    audio_file_source retained_source;
    int result;

    if (!file)
        return AUDIO_FILE_ERR_ARGUMENT;
    if (audio_operation_get() != 0)
        return AUDIO_FILE_ERR_STATE;
    if (!source) {
        *file = (audio_file){0};
        return AUDIO_FILE_ERR_ARGUMENT;
    }
    /* Retained-source reopen on an ALAC file is address-sensitive because
     * its exact-read adapter points back into the enclosing object. Reject a
     * shallow moved copy before clearing it or invoking that stale adapter. */
#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    if (source == &file->source && file->state == AUDIO_FILE_STATE_ALAC &&
        !alac_stream_has_stable_identity(&file->decoder.alac))
        return AUDIO_FILE_ERR_STATE;
#endif
    /* Although FLAC does not consume the two decoder-storage arguments, keep
     * the public open boundary deterministic: writable ranges supplied to it
     * may not overlap the destination or each other, and may not wrap. */
    if ((packet_storage && packet_capacity != 0u &&
         !audio_range_valid(packet_storage, packet_capacity)) ||
        (workspace && workspace_capacity != 0u &&
         !audio_range_valid(workspace, workspace_capacity)) ||
        audio_ranges_overlap(file, (unsigned)sizeof(*file), packet_storage,
                             packet_capacity) ||
        audio_ranges_overlap(file, (unsigned)sizeof(*file), workspace,
                             workspace_capacity) ||
        audio_ranges_overlap(packet_storage, packet_capacity, workspace,
                             workspace_capacity))
        return AUDIO_FILE_ERR_ARGUMENT;
    retained_source = *source;
    *file = (audio_file){0};
    if (!source_valid(&retained_source) || max_frames == 0u)
        return AUDIO_FILE_ERR_ARGUMENT;
    file->source = retained_source;
    if (!audio_operation_acquire(file))
        return AUDIO_FILE_ERR_STATE;
    result = read_magic(&file->source, magic);
    if (result != AUDIO_SOURCE_READ_OK) {
        result = map_magic_read_result(result);
        goto open_done;
    }

    if (magic[0] == 'f' && magic[1] == 'L' && magic[2] == 'a' &&
        magic[3] == 'C') {
        result = fill_flac_info(&file->source, &file->info);
        if (result != AUDIO_FILE_OK)
            goto open_done;
        if (file->info.total_frames != 0u &&
            file->info.total_frames > max_frames) {
            result = AUDIO_FILE_ERR_FRAME_LIMIT;
            goto open_done;
        }
        file->decoder.flac_max_frames = max_frames;
        file->state = AUDIO_FILE_STATE_FLAC_READY;
        result = AUDIO_FILE_OK;
        goto open_done;
    }

#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    setup_alac_source(file, &alac_source);
    result = alac_stream_open(&file->decoder.alac, &alac_source,
                              packet_storage, packet_capacity, workspace,
                              workspace_capacity, max_frames);
    if (result != ALAC_STREAM_OK)
        result = map_stream_result(result);
    else {
        fill_alac_info(&file->decoder.alac.container, &file->info);
        file->state = AUDIO_FILE_STATE_ALAC;
        result = AUDIO_FILE_OK;
    }
#else
    result = AUDIO_FILE_ERR_FORMAT;
#endif
open_done:
    audio_operation_release(file);
    return result;
}

static int sink_bridge(const int32_t * const channels[], unsigned frame_count,
                       unsigned channel_count, unsigned bits_per_sample,
                       void *user)
{
    audio_sink_context *context = (audio_sink_context *)user;

    if (!context || !context->sink ||
        (uint64_t)frame_count > context->max_frames - context->frames) {
        if (context)
            context->frame_limit = 1u;
        return 1;
    }
    if (context->sink(channels, frame_count, channel_count,
                      bits_per_sample, context->sink_user) != 0) {
        context->sink_error = 1u;
        return 1;
    }
    context->frames += frame_count;
    return 0;
}

int audio_file_decode(audio_file *file, audio_file_pcm_sink sink,
                      void *sink_user, uint64_t *frames_out)
{
    audio_sink_context context;
    int result;

    if (!frames_out)
        return AUDIO_FILE_ERR_ARGUMENT;
    if (audio_output_conflicts(file, frames_out,
                               (unsigned)sizeof(*frames_out)))
        return AUDIO_FILE_ERR_ARGUMENT;
    *frames_out = 0u;
    if (audio_operation_get() != 0)
        return AUDIO_FILE_ERR_STATE;
    if (!file || file->state == AUDIO_FILE_STATE_CLOSED || !sink)
        return AUDIO_FILE_ERR_ARGUMENT;
    if (file->state == AUDIO_FILE_STATE_FLAC_READY ||
        file->state == AUDIO_FILE_STATE_FLAC_CONSUMED) {
        if (file->state == AUDIO_FILE_STATE_FLAC_CONSUMED)
            return AUDIO_FILE_ERR_STATE;
        file->state = AUDIO_FILE_STATE_FLAC_CONSUMED;
    }

    if (!audio_operation_acquire(file))
        return AUDIO_FILE_ERR_STATE;
    context.sink = sink;
    context.sink_user = sink_user;
    context.frames = 0u;
#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    context.max_frames = file->state == AUDIO_FILE_STATE_ALAC
                             ? file->decoder.alac.max_frames
                             : file->decoder.flac_max_frames;
#else
    context.max_frames = file->decoder.flac_max_frames;
#endif
    context.frame_limit = 0u;
    context.sink_error = 0u;

    if (file->state != AUDIO_FILE_STATE_ALAC) {
        flac_mod_bounded_random_input input;
        flac_mod_info info;
        unsigned decoded = 0u;

        if (file->source.size > UINT_MAX) {
            result = AUDIO_FILE_ERR_RANGE;
            goto done;
        }
        input.read_at = flac_source_read_at;
        input.user = &file->source;
        input.base_offset = 0u;
        input.length = (unsigned)file->source.size;
        input.max_request = 0u;
        result = flac_mod_decode_file_at_bounded(
            &input, sink_bridge, &context, &info, &decoded);
        *frames_out = context.frames;
        if (context.frame_limit)
            result = AUDIO_FILE_ERR_FRAME_LIMIT;
        else if (context.sink_error)
            result = AUDIO_FILE_ERR_OUTPUT;
        else if (result != FLAC_MOD_OK)
            result = result == FLAC_MOD_ERR_UNSUPPORTED_FORMAT
                       ? AUDIO_FILE_ERR_UNSUPPORTED
                   : result == FLAC_MOD_ERR_IO
                       ? AUDIO_FILE_ERR_IO
                       : AUDIO_FILE_ERR_DECODER;
        else
            result = AUDIO_FILE_OK;
        goto done;
    }

#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    result = alac_stream_decode(&file->decoder.alac, sink_bridge, &context,
                                frames_out);
    *frames_out = context.frames;
    if (context.frame_limit)
        result = AUDIO_FILE_ERR_FRAME_LIMIT;
    else if (context.sink_error)
        result = AUDIO_FILE_ERR_OUTPUT;
    else if (result != ALAC_STREAM_OK)
        result = map_stream_result(result);
    else
        result = AUDIO_FILE_OK;
#else
    result = AUDIO_FILE_ERR_STATE;
#endif
done:
    audio_operation_release(file);
    return result;
}
