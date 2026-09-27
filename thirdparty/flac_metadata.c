#include "flac_metadata.h"

#include <stdint.h>

#define FLAC_SIGNATURE_0 'f'
#define FLAC_SIGNATURE_1 'L'
#define FLAC_SIGNATURE_2 'a'
#define FLAC_SIGNATURE_3 'C'
#define FLAC_STREAMINFO_LENGTH 34u
#define FLAC_MAX_METADATA_BLOCKS 1024u

static unsigned be16(const flac_metadata_u8 *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned be24(const flac_metadata_u8 *p)
{
    return ((unsigned)p[0] << 16) | ((unsigned)p[1] << 8) | p[2];
}

static int info_contains_user(const flac_metadata_info *info,
                              const void *user)
{
    uintptr_t info_address;
    uintptr_t user_address;

    if (!info || !user)
        return 0;
    info_address = (uintptr_t)info;
    user_address = (uintptr_t)user;
    return user_address >= info_address &&
           user_address - info_address < sizeof(*info);
}

static int read_exact(flac_metadata_read read, void *user, void *destination,
                      unsigned offset, unsigned count)
{
    unsigned got = 0;
    int status;

    if (!read || (!destination && count != 0))
        return FLAC_METADATA_ERR_ARGUMENT;
    status = read(user, destination, offset, count, &got);
    if (status != 0 || got != count)
        return FLAC_METADATA_ERR_IO;
    return FLAC_METADATA_OK;
}

static int parse_streaminfo(const flac_metadata_u8 *p,
                            flac_metadata_info *info)
{
    unsigned sample_rate;
    unsigned channels;
    unsigned bits_per_sample;
    unsigned total_samples;

    /* RFC 9639 reserves STREAMINFO block sizes below 16 samples.  Rejecting
     * those forbidden declarations here keeps the lightweight classifier in
     * step with the decoder before either path allocates playback state. */
    if (be16(p) < 16u || be16(p + 2) < 16u || be16(p + 2) < be16(p))
        return FLAC_METADATA_ERR_STREAMINFO;

    sample_rate = ((unsigned)p[10] << 12) |
                  ((unsigned)p[11] << 4) | (p[12] >> 4);
    channels = ((p[12] >> 1) & 7u) + 1u;
    bits_per_sample = (((p[12] & 1u) << 4) | (p[13] >> 4)) + 1u;
    /* Total samples is a 36-bit field.  The stock source/task fields are
     * 32-bit, so reject streams whose count cannot be represented there. */
    if ((p[13] & 0x0fu) != 0)
        return FLAC_METADATA_ERR_UNSUPPORTED;
    total_samples = ((unsigned)p[14] << 24) | ((unsigned)p[15] << 16) |
                    ((unsigned)p[16] << 8) | p[17];

    /* The XDJ audio path observed in the stock image is stereo/mono and
     * operates at up to 48 kHz.  Keep the policy here identical to the
     * already-tested native decoder adapter. */
    if (sample_rate == 0 || sample_rate > 48000 || channels == 0 ||
        channels > 2 || bits_per_sample < 4 || bits_per_sample > 24)
        return FLAC_METADATA_ERR_UNSUPPORTED;

    info->sample_rate = sample_rate;
    info->channels = channels;
    info->bits_per_sample = bits_per_sample;
    info->total_samples = total_samples;
    return FLAC_METADATA_OK;
}

int flac_metadata_parse(flac_metadata_read read, void *user,
                        flac_metadata_info *info)
{
    flac_metadata_u8 first[4 + 4 + FLAC_STREAMINFO_LENGTH];
    flac_metadata_u8 block_header[4];
    flac_metadata_u8 payload_tail;
    unsigned cursor;
    unsigned block_count;
    unsigned type;
    unsigned length;
    unsigned last;
    int result;

    if (!read || !info || info_contains_user(info, user))
        return FLAC_METADATA_ERR_ARGUMENT;
    info->sample_rate = 0;
    info->channels = 0;
    info->bits_per_sample = 0;
    info->total_samples = 0;
    info->frame_data_offset = 0;
    info->metadata_block_count = 0;

    result = read_exact(read, user, first, 0, sizeof(first));
    if (result != FLAC_METADATA_OK)
        return result;
    if (first[0] != FLAC_SIGNATURE_0 || first[1] != FLAC_SIGNATURE_1 ||
        first[2] != FLAC_SIGNATURE_2 || first[3] != FLAC_SIGNATURE_3)
        return FLAC_METADATA_ERR_SIGNATURE;

    last = first[4] & 0x80u;
    type = first[4] & 0x7fu;
    length = be24(first + 5);
    if (type != 0 || length != FLAC_STREAMINFO_LENGTH)
        return FLAC_METADATA_ERR_STREAMINFO;
    result = parse_streaminfo(first + 8, info);
    if (result != FLAC_METADATA_OK)
        return result;

    cursor = 4u + 4u + FLAC_STREAMINFO_LENGTH;
    block_count = 1;
    while (!last) {
        if (block_count >= FLAC_MAX_METADATA_BLOCKS)
            return FLAC_METADATA_ERR_BLOCKS;
        /* The previous block can end within the 32-bit domain while there
         * is no room for the next four-byte header. Check before presenting
         * that read to a source callback. */
        if (cursor > 0xFFFFFFFFu - (unsigned)sizeof(block_header))
            return FLAC_METADATA_ERR_OVERFLOW;
        result = read_exact(read, user, block_header, cursor,
                            sizeof(block_header));
        if (result != FLAC_METADATA_OK)
            return result;
        last = block_header[0] & 0x80u;
        type = block_header[0] & 0x7fu;
        length = be24(block_header + 1);
        if (type == 0 || type == 127)
            return FLAC_METADATA_ERR_BLOCKS;
        if (length > 0xFFFFFFFFu - cursor - 4u)
            return FLAC_METADATA_ERR_OVERFLOW;
        /* Metadata payloads are otherwise skipped, but their declared
         * extent still has to exist.  Touching the final byte preserves the
         * allocation-free random-access interface while preventing a final
         * block header from extending classification past source EOF. */
        if (length != 0u) {
            result = read_exact(read, user, &payload_tail,
                                cursor + 4u + length - 1u, 1u);
            if (result != FLAC_METADATA_OK)
                return result;
        }
        cursor += 4u + length;
        block_count++;
    }

    info->frame_data_offset = cursor;
    info->metadata_block_count = block_count;
    return FLAC_METADATA_OK;
}
