#ifndef XDJ700_FLAC_METADATA_H
#define XDJ700_FLAC_METADATA_H

/*
 * Small, allocation-free FLAC metadata reader.
 *
 * The parser knows only the public FLAC container format.  It deliberately
 * has no dependency on libFLAC or on the XDJ firmware so that the metadata
 * rules can be tested independently on the host and reused by the stock
 * parser ABI adapter.
 */

typedef unsigned char flac_metadata_u8;

typedef int (*flac_metadata_read)(void *user, void *destination,
                                 unsigned offset, unsigned count,
                                 unsigned *bytes_read);

typedef struct {
    unsigned sample_rate;
    unsigned channels;
    unsigned bits_per_sample;
    unsigned total_samples;
    unsigned frame_data_offset;
    unsigned metadata_block_count;
} flac_metadata_info;

enum {
    FLAC_METADATA_OK = 0,
    FLAC_METADATA_ERR_ARGUMENT = 1,
    FLAC_METADATA_ERR_IO = 2,
    FLAC_METADATA_ERR_SIGNATURE = 3,
    FLAC_METADATA_ERR_STREAMINFO = 4,
    FLAC_METADATA_ERR_UNSUPPORTED = 5,
    FLAC_METADATA_ERR_BLOCKS = 6,
    FLAC_METADATA_ERR_OVERFLOW = 7
};

/* Parse STREAMINFO and walk the metadata chain.  Payloads are not retained,
 * but the parser exact-reads each non-empty block's final byte so a declared
 * metadata extent beyond source EOF cannot be accepted. The callback user
 * pointer must not point into info: that alias is rejected before any output
 * byte is cleared or any read callback is invoked. Every callback request has
 * a nonwrapping 32-bit offset plus count; an unavailable next header is
 * reported as FLAC_METADATA_ERR_OVERFLOW before invoking the callback. */
int flac_metadata_parse(flac_metadata_read read, void *user,
                        flac_metadata_info *info);

#endif
