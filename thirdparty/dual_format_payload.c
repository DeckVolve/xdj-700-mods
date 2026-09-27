#include "dual_format_payload.h"

#if defined(XDJ700_TARGET_RUNTIME_PROVIDER) && XDJ700_TARGET_RUNTIME_PROVIDER
#include "dual_format_runtime.h"
#endif

typedef char shared_storage_partition_must_fit[
    DUAL_FORMAT_PAYLOAD_ALAC_WORKSPACE_SIZE <
        DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_SIZE ? 1 : -1];
typedef char shared_storage_alignment_must_be_power_of_two[
    DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT != 0u &&
    (DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT &
     (DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT - 1u)) == 0u ? 1 : -1];

static int range_end(uintptr_t start, unsigned count, uintptr_t *end)
{
    if (!end || start > UINTPTR_MAX - count)
        return 0;
    *end = start + count;
    return 1;
}

static int ranges_overlap(const void *left, unsigned left_count,
                          const void *right, unsigned right_count)
{
    uintptr_t left_start = (uintptr_t)left;
    uintptr_t right_start = (uintptr_t)right;
    uintptr_t left_end;
    uintptr_t right_end;

    if (left_count == 0u || right_count == 0u)
        return 0;
    /* A nonempty span whose base/length is not representable is unsafe even
     * when its virtual base differs from the other span.  Treat it as an
     * overlap so every public output/descriptor guard fails closed before it
     * can clear or copy through a wrapped pointer. */
    if (!left || !right ||
        !range_end(left_start, left_count, &left_end) ||
        !range_end(right_start, right_count, &right_end))
        return 1;
    return left_start < right_end && right_start < left_end;
}

static int storage_range_overlap(const void *memory, unsigned count,
                                 const dual_format_payload_storage *storage)
{
    if (!storage)
        return 0;
    return ranges_overlap(memory, count, storage->flac_arena,
                          storage->flac_arena_capacity) ||
           ranges_overlap(memory, count, storage->alac_workspace,
                          storage->alac_workspace_capacity) ||
           ranges_overlap(memory, count, storage->alac_packet_storage,
                          storage->alac_packet_capacity);
}

static int public_range_conflicts(
    const void *memory, unsigned count,
    const dual_format_payload_session *session,
    const dual_format_payload_source *source,
    const dual_format_payload_storage *storage)
{
    return (session && ranges_overlap(memory, count, session,
                                      (unsigned)sizeof(*session))) ||
           (source && ranges_overlap(memory, count, source,
                                     (unsigned)sizeof(*source))) ||
           (storage && ranges_overlap(memory, count, storage,
                                      (unsigned)sizeof(*storage))) ||
           storage_range_overlap(memory, count, storage);
}

int dual_format_payload_bind_shared_storage(
    dual_format_payload_storage *storage, void *memory,
    unsigned memory_capacity, uint64_t max_frames)
    __attribute__((used, section(".text.dual_format_payload_bind_shared_storage")));

int dual_format_payload_bind_shared_storage(
    dual_format_payload_storage *storage, void *memory,
    unsigned memory_capacity, uint64_t max_frames)
{
    uintptr_t address;
    uintptr_t aligned;
    unsigned adjustment;

    if (!storage)
        return AUDIO_FILE_ERR_ARGUMENT;
    *storage = (dual_format_payload_storage){0};
    if (!memory || max_frames == 0u)
        return AUDIO_FILE_ERR_ARGUMENT;

    address = (uintptr_t)memory;
    adjustment = (unsigned)
        ((DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT -
          (address & (DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT - 1u))) &
         (DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_ALIGNMENT - 1u));
    if (address > UINTPTR_MAX - adjustment ||
        memory_capacity < adjustment ||
        memory_capacity - adjustment <
            DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_SIZE)
        return AUDIO_FILE_ERR_WORKSPACE;
    aligned = address + adjustment;
    if (aligned > UINTPTR_MAX -
                      DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_SIZE)
        return AUDIO_FILE_ERR_WORKSPACE;

    storage->flac_arena = (void *)aligned;
    storage->flac_arena_capacity =
        DUAL_FORMAT_PAYLOAD_SHARED_STORAGE_SIZE;
    storage->alac_workspace = (void *)aligned;
    storage->alac_workspace_capacity =
        DUAL_FORMAT_PAYLOAD_ALAC_WORKSPACE_SIZE;
    storage->alac_packet_storage = (unsigned char *)aligned +
        DUAL_FORMAT_PAYLOAD_ALAC_WORKSPACE_SIZE;
    storage->alac_packet_capacity = DUAL_FORMAT_PAYLOAD_ALAC_PACKET_SIZE;
    storage->max_frames = max_frames;
    return AUDIO_FILE_OK;
}

int dual_format_payload_session_open(
    dual_format_payload_session *session,
    const dual_format_payload_source *source,
    const dual_format_payload_storage *storage,
    dual_format_payload_info *info_out)
    __attribute__((used, section(".text.dual_format_payload_session_open")));

int dual_format_payload_session_next(
    dual_format_payload_session *session,
    dual_format_payload_pcm_sink sink,
    void *sink_user,
    unsigned *frames_delivered)
    __attribute__((used, section(".text.dual_format_payload_session_next")));

void dual_format_payload_session_close(
    dual_format_payload_session *session)
    __attribute__((used, section(".text.dual_format_payload_session_close")));

static int flac_arena_valid(const dual_format_payload_storage *storage)
{
    uintptr_t address;

    if (!storage || !storage->flac_arena ||
        storage->flac_arena_capacity <
            DUAL_FORMAT_PAYLOAD_FLAC_ARENA_SIZE)
        return 0;
    address = (uintptr_t)storage->flac_arena;
    return (address &
            (DUAL_FORMAT_PAYLOAD_FLAC_ARENA_ALIGNMENT - 1u)) == 0u;
}

enum {
    PAYLOAD_SESSION_CLOSED = 0u,
    PAYLOAD_SESSION_OPENING = 0x504C4F50u, /* "PLOP" */
    PAYLOAD_SESSION_ACTIVE = 0x504C4143u,  /* "PLAC" */
    PAYLOAD_SESSION_ENDED = 0x504C454Eu,   /* "PLEN" */
    PAYLOAD_SESSION_ERROR = 0x504C4552u    /* "PLER" */
};

#if defined(XDJ700_TARGET_RUNTIME_PROVIDER) && XDJ700_TARGET_RUNTIME_PROVIDER
static dual_format_payload_session *payload_owner_get(void)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();

    return runtime != 0
        ? (dual_format_payload_session *)runtime->payload_active_session
        : 0;
}

static int payload_owner_acquire(dual_format_payload_session *session)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();

    if (runtime == 0 || runtime->payload_active_session != 0)
        return 0;
    runtime->payload_active_session = session;
    return 1;
}

static void payload_owner_release(dual_format_payload_session *session)
{
    dual_format_runtime_state *runtime = dual_format_runtime_current();

    if (runtime != 0 && runtime->payload_active_session == session)
        runtime->payload_active_session = 0;
}
#else
static dual_format_payload_session *payload_active_session;

static dual_format_payload_session *payload_owner_get(void)
{
    return payload_active_session;
}

static int payload_owner_acquire(dual_format_payload_session *session)
{
    if (payload_active_session != 0)
        return 0;
    payload_active_session = session;
    return 1;
}

static void payload_owner_release(dual_format_payload_session *session)
{
    if (payload_active_session == session)
        payload_active_session = 0;
}
#endif

typedef struct {
    dual_format_payload_pcm_sink sink;
    void *sink_user;
    uint64_t frames_before;
    uint64_t max_frames;
    unsigned delivered;
    unsigned calls;
    unsigned frame_limit;
    unsigned sink_error;
    unsigned protocol_error;
} payload_sink_context;

static int payload_sink_bridge(const int32_t * const channels[],
                               unsigned frame_count,
                               unsigned channel_count,
                               unsigned bits_per_sample,
                               void *user)
{
    payload_sink_context *context = (payload_sink_context *)user;

    if (!context || !context->sink || context->calls != 0u ||
        !channels || !channels[0] || frame_count == 0u ||
        channel_count == 0u || channel_count > 2u ||
        (channel_count == 2u && !channels[1])) {
        if (context)
            context->protocol_error = 1u;
        return 1;
    }
    context->calls++;
    if (context->frames_before > context->max_frames ||
        (uint64_t)frame_count >
            context->max_frames - context->frames_before) {
        context->frame_limit = 1u;
        return 1;
    }
    if (context->sink(channels, frame_count, channel_count,
                      bits_per_sample, context->sink_user) != 0) {
        context->sink_error = 1u;
        return 1;
    }
    context->delivered = frame_count;
    return 0;
}

static int payload_flac_read(void *user, unsigned char *destination,
                             unsigned count)
{
    dual_format_payload_session *session =
        (dual_format_payload_session *)user;
    const dual_format_payload_source *source;
    uint64_t remaining;
    unsigned request;
    unsigned bytes_read = 0u;
    int result;

    if (!session || payload_owner_get() != session || !session->source ||
        !session->source->read_at || (!destination && count != 0u))
        return -1;
    source = session->source;
    if (session->flac_offset > source->size)
        return -1;
    remaining = source->size - session->flac_offset;
    request = count;
    if ((uint64_t)request > remaining)
        request = (unsigned)remaining;
    if (request == 0u)
        return 0;

    result = source->read_at(source->read_user, session->flac_offset,
                             destination, request, &bytes_read);
    /* This adapter retains an exact source length, and request is nonzero
     * whenever the cursor is below it.  A successful zero-byte callback here
     * is therefore transport failure rather than physical EOF.  Passing it to
     * libFLAC as END_OF_STREAM could accept a stalled source after the final
     * declared PCM frame while unread source bytes still remain. */
    if (result != 0 || bytes_read == 0u || bytes_read > request ||
        bytes_read > 0x7FFFFFFFu)
        return -1;
    /* request is bounded by source->size - flac_offset above, so this
     * uint64_t addition cannot overflow.  Keep the neutral source path
     * 64-bit clean; only concrete stock-ABI adapters narrow offsets. */
    session->flac_offset += bytes_read;
    return (int)bytes_read;
}

#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
static int map_alac_stream_result(int result)
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
    case ALAC_STREAM_ERR_DECODER:
    default:
        return AUDIO_FILE_ERR_FORMAT;
    }
}
#endif

static int map_flac_session_result(int result,
                                   const payload_sink_context *context)
{
    if (context && context->frame_limit)
        return AUDIO_FILE_ERR_FRAME_LIMIT;
    if (context && context->sink_error)
        return AUDIO_FILE_ERR_OUTPUT;
    if (context && context->protocol_error)
        return AUDIO_FILE_ERR_DECODER;
    if (result == FLAC_MOD_ERR_UNSUPPORTED_FORMAT)
        return AUDIO_FILE_ERR_UNSUPPORTED;
    if (result == FLAC_MOD_ERR_IO)
        return AUDIO_FILE_ERR_IO;
    if (result == FLAC_MOD_ERR_STATE)
        return AUDIO_FILE_ERR_STATE;
    if (result == FLAC_MOD_ERR_ALLOC || result == FLAC_MOD_ERR_INIT)
        return AUDIO_FILE_ERR_WORKSPACE;
    return AUDIO_FILE_ERR_DECODER;
}

static void payload_session_release(dual_format_payload_session *session)
{
    if (!session)
        return;
    if (session->flac_opened) {
        flac_mod_session_close(&session->flac);
        session->flac_opened = 0u;
    }
    payload_owner_release(session);
    *session = (dual_format_payload_session){0};
}

int dual_format_payload_session_open(
    dual_format_payload_session *session,
    const dual_format_payload_source *source,
    const dual_format_payload_storage *storage,
    dual_format_payload_info *info_out)
{
    audio_file_source audio_source;
    int result;

    if (!session) {
        if (info_out)
            *info_out = (dual_format_payload_info){0};
        return AUDIO_FILE_ERR_ARGUMENT;
    }
    /* Reject every writable alias before clearing output or session bytes.
     * In particular, callback-reentrant open with info_out inside the active
     * session must not corrupt the owner before the STATE check below. */
    if ((source && ranges_overlap(session, (unsigned)sizeof(*session),
                                  source, (unsigned)sizeof(*source))) ||
        (storage &&
         (ranges_overlap(session, (unsigned)sizeof(*session), storage,
                         (unsigned)sizeof(*storage)) ||
          storage_range_overlap(session, (unsigned)sizeof(*session),
                                storage))) ||
        (info_out &&
         (public_range_conflicts(info_out, (unsigned)sizeof(*info_out),
                                 session, source, storage))))
        return AUDIO_FILE_ERR_ARGUMENT;
    if (info_out)
        *info_out = (dual_format_payload_info){0};
    /* Acquire ownership before clearing or reading anything.  In particular,
     * a source callback that tries to open another session observes STATE.
     * This non-atomic owner is a reentrancy guard, not a replacement for the
     * caller's task confinement or external lock. */
    if (payload_owner_get() != 0)
        return AUDIO_FILE_ERR_STATE;
    *session = (dual_format_payload_session){0};
    if (!source || !storage || !source->read_at ||
        storage->max_frames == 0u)
        return AUDIO_FILE_ERR_ARGUMENT;

    if (!payload_owner_acquire(session))
        return AUDIO_FILE_ERR_STATE;
    session->state = PAYLOAD_SESSION_OPENING;
    session->operation_active = 1u;
    session->source = source;
    session->storage = storage;

    audio_source.read_at = source->read_at;
    audio_source.user = source->read_user;
    audio_source.size = source->size;
    result = audio_file_open(&session->file, &audio_source,
                             storage->alac_packet_storage,
                             storage->alac_packet_capacity,
                             storage->alac_workspace,
                             storage->alac_workspace_capacity,
                             storage->max_frames);
    if (result != AUDIO_FILE_OK)
        goto fail;
    if (session->file.info.kind == AUDIO_FILE_KIND_FLAC) {
        flac_mod_input input;
        int flac_result;

        if (!flac_arena_valid(storage)) {
            result = AUDIO_FILE_ERR_WORKSPACE;
            goto fail;
        }
        flac_result = flac_mod_init_arena(storage->flac_arena,
                                          storage->flac_arena_capacity);
        if (flac_result == FLAC_MOD_ERR_INIT) {
            result = AUDIO_FILE_ERR_STATE;
            goto fail;
        }
        if (flac_result != FLAC_MOD_OK) {
            result = AUDIO_FILE_ERR_WORKSPACE;
            goto fail;
        }
        input.read = payload_flac_read;
        input.user = session;
        flac_result = flac_mod_session_open(&session->flac, &input);
        if (flac_result != FLAC_MOD_OK) {
            result = map_flac_session_result(flac_result, 0);
            goto fail;
        }
        session->flac_opened = 1u;
    }

    session->operation_active = 0u;
    if (info_out)
        *info_out = session->file.info;
    session->state = PAYLOAD_SESSION_ACTIVE;
    return AUDIO_FILE_OK;

fail:
    session->operation_active = 0u;
    payload_session_release(session);
    return result;
}

int dual_format_payload_session_next(
    dual_format_payload_session *session,
    dual_format_payload_pcm_sink sink,
    void *sink_user,
    unsigned *frames_delivered)
{
    payload_sink_context context;
    unsigned decoder_frames = 0u;
    int result;
    int mapped_error = AUDIO_FILE_OK;

    if (!session || !sink || !frames_delivered) {
        if (frames_delivered)
            *frames_delivered = 0u;
        return DUAL_FORMAT_PAYLOAD_NEXT_ERROR(AUDIO_FILE_ERR_ARGUMENT);
    }
    if (ranges_overlap(frames_delivered,
                       (unsigned)sizeof(*frames_delivered), session,
                       (unsigned)sizeof(*session)))
        return DUAL_FORMAT_PAYLOAD_NEXT_ERROR(AUDIO_FILE_ERR_ARGUMENT);
    if (payload_owner_get() != session ||
        session->state != PAYLOAD_SESSION_ACTIVE) {
        *frames_delivered = 0u;
        return DUAL_FORMAT_PAYLOAD_NEXT_ERROR(AUDIO_FILE_ERR_STATE);
    }
    if (public_range_conflicts(frames_delivered,
                               (unsigned)sizeof(*frames_delivered), 0,
                               session->source, session->storage))
        return DUAL_FORMAT_PAYLOAD_NEXT_ERROR(AUDIO_FILE_ERR_ARGUMENT);
    *frames_delivered = 0u;
    if (session->operation_active)
        return DUAL_FORMAT_PAYLOAD_NEXT_ERROR(AUDIO_FILE_ERR_STATE);

    context = (payload_sink_context){0};
    context.sink = sink;
    context.sink_user = sink_user;
    context.frames_before = session->frames_delivered;
    context.max_frames = session->storage->max_frames;
    session->operation_active = 1u;

    if (session->file.info.kind == AUDIO_FILE_KIND_FLAC) {
        flac_mod_info flac_info;

        result = flac_mod_session_next(&session->flac,
                                       payload_sink_bridge, &context,
                                       &flac_info, &decoder_frames);
        if (result == FLAC_MOD_END) {
            if (context.calls != 0u || context.delivered != 0u)
                mapped_error = AUDIO_FILE_ERR_DECODER;
            else
                result = DUAL_FORMAT_PAYLOAD_NEXT_END;
        } else if (result == FLAC_MOD_OK) {
            if (context.calls != 1u || context.delivered == 0u ||
                decoder_frames != context.delivered)
                mapped_error = AUDIO_FILE_ERR_DECODER;
            else
                result = DUAL_FORMAT_PAYLOAD_NEXT_BLOCK;
        } else {
            mapped_error = map_flac_session_result(result, &context);
        }
    }
#if !defined(XDJ700_FLAC_ONLY_PAYLOAD) || !XDJ700_FLAC_ONLY_PAYLOAD
    else {
        result = alac_stream_next(&session->file.decoder.alac,
                                  payload_sink_bridge, &context,
                                  &decoder_frames);
        if (result == ALAC_STREAM_END) {
            if (context.calls != 0u || context.delivered != 0u)
                mapped_error = AUDIO_FILE_ERR_DECODER;
            else
                result = DUAL_FORMAT_PAYLOAD_NEXT_END;
        } else if (result == ALAC_STREAM_OK) {
            if (context.calls != 1u || context.delivered == 0u ||
                decoder_frames != context.delivered)
                mapped_error = AUDIO_FILE_ERR_DECODER;
            else
                result = DUAL_FORMAT_PAYLOAD_NEXT_BLOCK;
        } else {
            mapped_error = map_alac_stream_result(result);
            if (context.frame_limit)
                mapped_error = AUDIO_FILE_ERR_FRAME_LIMIT;
            else if (context.sink_error)
                mapped_error = AUDIO_FILE_ERR_OUTPUT;
            else if (context.protocol_error)
                mapped_error = AUDIO_FILE_ERR_DECODER;
        }
    }
#else
    else {
        mapped_error = AUDIO_FILE_ERR_STATE;
    }
#endif

    if (context.delivered != 0u) {
        session->frames_delivered += context.delivered;
        *frames_delivered = context.delivered;
    }
    session->operation_active = 0u;
    if (mapped_error != AUDIO_FILE_OK) {
        session->state = PAYLOAD_SESSION_ERROR;
        return DUAL_FORMAT_PAYLOAD_NEXT_ERROR(mapped_error);
    }
    if (result == DUAL_FORMAT_PAYLOAD_NEXT_END)
        session->state = PAYLOAD_SESSION_ENDED;
    return result;
}

void dual_format_payload_session_close(
    dual_format_payload_session *session)
{
    dual_format_payload_session *owner;

    if (!session)
        return;
    owner = payload_owner_get();
    if (owner != session) {
        /* A callback-supplied non-owner may be an interior pointer into the
         * live owner, its retained descriptors, or decoder storage.  Do not
         * turn harmless cancellation into an overlapping memset. */
        if (owner && public_range_conflicts(
                         session, (unsigned)sizeof(*session), owner,
                         owner->source, owner->storage))
            return;
        *session = (dual_format_payload_session){0};
        return;
    }
    /* A callback cannot tear down decoder-owned planes or callback stack
     * state.  The owning caller can close immediately after next returns. */
    if (session->operation_active)
        return;
    payload_session_release(session);
}

int dual_format_payload_decode(const dual_format_payload_source *source,
                               const dual_format_payload_storage *storage,
                               dual_format_payload_pcm_sink sink,
                               void *sink_user,
                               dual_format_payload_info *info_out,
                               uint64_t *frames_out)
{
    dual_format_payload_session session;
    uint64_t decoded_frames = 0u;
    int result;

    if ((info_out && public_range_conflicts(
                         info_out, (unsigned)sizeof(*info_out), 0,
                         source, storage)) ||
        (frames_out && public_range_conflicts(
                           frames_out, (unsigned)sizeof(*frames_out), 0,
                           source, storage)) ||
        (info_out && frames_out &&
         ranges_overlap(info_out, (unsigned)sizeof(*info_out), frames_out,
                        (unsigned)sizeof(*frames_out))))
        return AUDIO_FILE_ERR_ARGUMENT;
    if (info_out)
        *info_out = (dual_format_payload_info){0};
    if (frames_out)
        *frames_out = 0u;
    if (!source || !storage || !source->read_at || !sink)
        return AUDIO_FILE_ERR_ARGUMENT;

    result = dual_format_payload_session_open(&session, source, storage,
                                              info_out);
    if (result != AUDIO_FILE_OK)
        return result;

    for (;;) {
        unsigned block_frames = 0u;
        int next_result = dual_format_payload_session_next(
            &session, sink, sink_user, &block_frames);

        decoded_frames += block_frames;
        if (next_result == DUAL_FORMAT_PAYLOAD_NEXT_BLOCK)
            continue;
        if (next_result == DUAL_FORMAT_PAYLOAD_NEXT_END)
            result = AUDIO_FILE_OK;
        else
            result = -next_result;
        break;
    }
    if (frames_out)
        *frames_out = decoded_frames;
    dual_format_payload_session_close(&session);
    return result;
}
