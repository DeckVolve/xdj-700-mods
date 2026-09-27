#ifndef XDJ700_STOCK_LOSSLESS_ADAPTER_H
#define XDJ700_STOCK_LOSSLESS_ADAPTER_H

/*
 * XDJ-700 v1.15 retained-source integration boundary.
 *
 * This is independently written glue between the public-format router/WAVE
 * bridge and the already recovered XDJ-700 storage call shapes.  It contains
 * no codec implementation from another device.  The entry points are kept
 * separate so a strict emulator regression can redirect only the audited
 * dispatcher, WAVE-reader, producer, and close call sites.
 *
 * These functions are not a firmware image and their presence in a linked
 * payload is not a flashability or hardware-safety claim.
 */

#include <stdint.h>

/* Host/probe builds enter here directly.  Integrity-gated target builds keep
 * this compatibility symbol inert; their checked assembly ingress invokes a
 * hidden implementation only after validating the gate return provenance. */
void stock_lossless_adapter_dispatch(void *manager, void *request);

/* Entry replacement for the stock five-argument parser read wrapper. */
int stock_lossless_adapter_read(void *descriptor, void *destination,
                                unsigned offset, unsigned count,
                                unsigned *bytes_read);

/* Caller-local replacements.  Every non-active object is delegated to the
 * corresponding unmodified stock entry. */
int stock_lossless_adapter_size(void *file, unsigned *size_out, void *aux);
int stock_lossless_adapter_position(void *file, void *aux);
/* Active retained files accept in-range absolute, relative, and end-relative
 * seeks.  A later read below the replay floor reopens the bridge lazily;
 * rejected targets leave the virtual cursor unchanged. */
int stock_lossless_adapter_seek(void *file, int offset, int origin, void *aux);
int stock_lossless_adapter_eof(void *file, void *aux);
int stock_lossless_adapter_transfer(void *destination, int operation,
                                    unsigned count, void *file, void *context);
int stock_lossless_adapter_close(void *file, void *arg1,
                                 void *arg2, void *arg3);

/* Assembly entries retained in the flat payload. */
void stock_lossless_dispatch_trampoline(void);
void stock_lossless_read_entry(void);

#endif
