#ifndef XDJ700_ALAC_MOD_H
#define XDJ700_ALAC_MOD_H

/*
 * Independent, allocation-free ALAC packet decoder.
 *
 * This interface deliberately stops at the public ALAC codec boundary.  It
 * accepts a parsed codec cookie and one elementary ALAC packet; container
 * demuxing and the XDJ task/PCM ABI remain separate adapters.  The caller
 * owns the workspace, which makes the decoder suitable for a bounded SH-4
 * arena as well as host tests.
 */

#include <stdint.h>

#define ALAC_MOD_CONFIG_BYTES 24u
#define ALAC_MOD_MAX_COOKIE_BYTES 80u
#define ALAC_MOD_MAX_FRAME_SAMPLES 4096u
/* Two channels, three int32_t planes per channel, plus the decoder's
 * alignment slack.  This is the largest workspace accepted by the target
 * policy and is useful to callers that bind one reusable format arena. */
#define ALAC_MOD_MAX_WORKSPACE_BYTES 98320u

typedef struct {
    unsigned frame_length;
    unsigned compatible_version;
    unsigned bit_depth;
    unsigned history_mult;
    unsigned initial_history;
    unsigned rice_limit;
    unsigned channels;
    unsigned max_run;          /* Publicly unused; normally encoded as 255. */
    unsigned max_frame_bytes;  /* 0 means unknown; otherwise bounds packets. */
    unsigned average_bitrate;
    unsigned sample_rate;
} alac_mod_config;

/* The planes contain signed samples right-aligned in int32_t.  This is the
 * same neutral representation used by the independent FLAC adapter; a later
 * device adapter can choose the stock PCM packing without changing the
 * codec.
 *
 * A sink callback is synchronous.  The channels array and every plane it
 * points to are decoder-workspace storage and are valid only until the
 * callback returns; a sink must consume or copy samples before returning and
 * must not retain or modify those pointers.  The callback is invoked at most
 * once per successful packet decode.  It must not re-enter a decode using the
 * same workspace/configuration (or re-enter an alac_stream using that
 * workspace).  A nonzero return rejects the decoded output. */
typedef int (*alac_mod_pcm_sink)(const int32_t * const channels[],
                                 unsigned frame_count,
                                 unsigned channel_count,
                                 unsigned bits_per_sample,
                                 void *user);

enum {
    ALAC_MOD_OK = 0,
    ALAC_MOD_ERR_ARGUMENT = 1,
    ALAC_MOD_ERR_COOKIE = 2,
    ALAC_MOD_ERR_UNSUPPORTED = 3,
    ALAC_MOD_ERR_WORKSPACE = 4,
    ALAC_MOD_ERR_PACKET = 5,
    ALAC_MOD_ERR_OUTPUT = 6
};

/* Process-wide diagnostic status, overwritten by each parser/decoder call;
 * it is not per-instance or thread-safe.  Code that relies on this value
 * must serialize the relevant calls and read it before another call. */
extern volatile unsigned alac_mod_last_error;

/* Parse a bare 24-byte ALACSpecificConfig, a 36- or 60-byte QuickTime 'alac'
 * atom, or the older 'frma' + 'alac' atom wrapper.  The declared cookie range
 * must be valid and disjoint from the config output; invalid ranges or overlap
 * fail before either object is read or written.  The documented optional
 * channel-layout/terminator bytes are ignored after the specific config, but
 * the complete supplied cookie is bounded by ALAC_MOD_MAX_COOKIE_BYTES. */
int alac_mod_parse_cookie(const unsigned char *cookie, unsigned cookie_length,
                          alac_mod_config *config);

/* Return the required workspace size, or zero for an invalid or overflowing
 * configuration.  The size includes enough slack for the decoder to align an
 * otherwise unaligned caller buffer to a four-byte boundary. */
unsigned alac_mod_workspace_bytes(const alac_mod_config *config);

/* Target policy used by the eventual XDJ adapter.  The codec parser itself
 * also accepts 32-bit ALAC so that unsupported device formats fail at the
 * adapter boundary rather than in packet syntax handling. */
int alac_mod_is_xdj_compatible(const alac_mod_config *config);

/* Decode one complete elementary ALAC packet.  A successful call invokes the
 * sink once (if non-NULL) and stores the frame count in frames_out.  A NULL
 * sink discards the decoded PCM; the stream adapter requires a non-NULL sink.
 * frames_out must be a valid unsigned-sized range disjoint from config, the
 * declared packet bytes, and the declared workspace bytes.  Invalid or
 * overlapping ranges fail before frames_out or those inputs are modified.
 * The sink callback contract above applies for the complete duration of the
 * call. */
int alac_mod_decode_packet(const alac_mod_config *config,
                           const unsigned char *packet,
                           unsigned packet_length,
                           void *workspace,
                           unsigned workspace_length,
                           alac_mod_pcm_sink sink,
                           void *sink_user,
                           unsigned *frames_out);

#endif
