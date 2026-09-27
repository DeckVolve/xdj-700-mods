#ifndef XDJ700_DUAL_FORMAT_RUNTIME_H
#define XDJ700_DUAL_FORMAT_RUNTIME_H

#include <stdint.h>

#include "flac_mod.h"

/*
 * Optional target-runtime storage for the stock adapter build.
 *
 * Neutral host and direct payload builds keep their historical translation-
 * unit globals.  The XDJ-700 stock adapter build defines
 * XDJ700_TARGET_RUNTIME_PROVIDER and supplies this object from its native
 * allocation, so the flat firmware payload does not depend on a guessed
 * fixed-address writable BSS range.
 */
typedef struct {
    void *payload_active_session;
    void *audio_file_active_operation;
    void *alac_stream_active_operation;
    flac_mod_session *flac_active_session;
    unsigned char *flac_heap_begin;
    unsigned char *flac_heap_end;
    unsigned char *flac_heap_ptr;
    unsigned flac_heap_active;
    volatile unsigned flac_mod_last_error;
    volatile unsigned alac_mod_last_error;
} dual_format_runtime_state;

dual_format_runtime_state *dual_format_runtime_current(void);

#endif
