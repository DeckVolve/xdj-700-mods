/* FLAC-only target routing. Other formats remain on the native stock path. */

#include "retained_format_router.h"

#include <stdint.h>

#define MAX_METADATA_BLOCKS 128u
#define MAX_FLAC_BLOCK_SAMPLES 4608u

static uint16_t be16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be24(const unsigned char *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t be64(const unsigned char *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static int read_exact(const retained_format_router_source *source,
                      uint64_t offset, void *destination, unsigned count)
{
    unsigned bytes_read = 0u;

    if (!source || !source->read_at || !destination ||
        offset > source->size || (uint64_t)count > source->size - offset)
        return 0;
    if (source->read_at(source->user, offset, destination, count,
                        &bytes_read) != 0)
        return 0;
    return bytes_read == count;
}

static retained_format_route classify_flac(
    const retained_format_router_source *source)
{
    uint64_t offset = 4u;
    unsigned block_index;
    int saw_last = 0;
    unsigned char header[4];
    unsigned char streaminfo[34];
    unsigned char frame_sync[2];

    for (block_index = 0u; block_index < MAX_METADATA_BLOCKS; ++block_index) {
        unsigned type;
        unsigned length;
        uint64_t data_offset;

        if (!read_exact(source, offset, header, sizeof(header)))
            return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
        type = header[0] & 0x7fu;
        length = be24(header + 1u);
        data_offset = offset + sizeof(header);
        if (type == 127u || data_offset > source->size ||
            (uint64_t)length > source->size - data_offset)
            return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
        if (block_index == 0u) {
            uint16_t min_block;
            uint16_t max_block;
            uint32_t min_frame;
            uint32_t max_frame;
            uint64_t packed;
            uint64_t total_samples;
            uint64_t max_wave_frames;
            unsigned sample_rate;
            unsigned channels;
            unsigned bits_per_sample;
            unsigned frame_bytes;

            if (type != 0u || length != sizeof(streaminfo) ||
                !read_exact(source, data_offset, streaminfo,
                            sizeof(streaminfo)))
                return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
            min_block = be16(streaminfo);
            max_block = be16(streaminfo + 2u);
            min_frame = be24(streaminfo + 4u);
            max_frame = be24(streaminfo + 7u);
            packed = be64(streaminfo + 10u);
            sample_rate = (unsigned)(packed >> 44);
            channels = (unsigned)((packed >> 41) & 7u) + 1u;
            bits_per_sample = (unsigned)((packed >> 36) & 31u) + 1u;
            total_samples = packed & UINT64_C(0x0000000fffffffff);
            if (bits_per_sample == 16u || bits_per_sample == 24u) {
                frame_bytes = channels * (bits_per_sample / 8u);
                max_wave_frames = ((uint64_t)UINT32_MAX - 44u) / frame_bytes;
            } else {
                max_wave_frames = 0u;
            }

            if (min_block < 16u || max_block < min_block ||
                max_block > MAX_FLAC_BLOCK_SAMPLES ||
                (min_frame != 0u && max_frame != 0u && max_frame < min_frame) ||
                (sample_rate != 44100u && sample_rate != 48000u) ||
                channels != 2u ||
                (bits_per_sample != 16u && bits_per_sample != 24u) ||
                total_samples == 0u || total_samples > max_wave_frames)
                return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
        } else if (type == 0u) {
            return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
        }

        offset = data_offset + length;
        if ((header[0] & 0x80u) != 0u) {
            saw_last = 1;
            break;
        }
    }
    /* Match audio_file_open's minimum first-frame sync preflight. */
    if (!saw_last || offset > source->size ||
        source->size - offset < sizeof(frame_sync) ||
        !read_exact(source, offset, frame_sync, sizeof(frame_sync)) ||
        frame_sync[0] != 0xffu || (frame_sync[1] & 0xfeu) != 0xf8u)
        return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
    return RETAINED_FORMAT_ROUTE_BRIDGE_FLAC;
}

retained_format_route retained_format_router_classify_detailed_bounded(
    const retained_format_router_source *source, uint64_t required_alac_size,
    unsigned max_alac_work)
{
    unsigned char prefix[12];
    uint64_t declared_end;

    (void)required_alac_size;
    (void)max_alac_work;
    if (!source || !source->read_at || source->size < sizeof(prefix) ||
        !read_exact(source, 0u, prefix, sizeof(prefix)))
        return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
    if (prefix[0] == 'R' && prefix[1] == 'I' && prefix[2] == 'F' &&
        prefix[3] == 'F') {
        declared_end = (uint64_t)prefix[4] | ((uint64_t)prefix[5] << 8) |
                       ((uint64_t)prefix[6] << 16) | ((uint64_t)prefix[7] << 24);
        declared_end += 8u;
        if (prefix[8] != 'W' || prefix[9] != 'A' || prefix[10] != 'V' ||
            prefix[11] != 'E' || declared_end < 12u ||
            declared_end > UINT32_MAX || declared_end > source->size)
            return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
        return RETAINED_FORMAT_ROUTE_STOCK_WAVE;
    }
    if (prefix[0] == 'f' && prefix[1] == 'L' && prefix[2] == 'a' &&
        prefix[3] == 'C')
        return classify_flac(source);
    return RETAINED_FORMAT_ROUTE_DEFER_STOCK;
}

retained_format_route retained_format_router_classify_detailed(
    const retained_format_router_source *source)
{
    return retained_format_router_classify_detailed_bounded(source, 0u, 0u);
}

retained_format_route retained_format_router_classify(
    const retained_format_router_source *source)
{
    return retained_format_router_classify_detailed(source);
}
