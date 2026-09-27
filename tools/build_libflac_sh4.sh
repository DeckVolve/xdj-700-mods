#!/bin/bash
# Cross-compile libFLAC decode path for SH-4 LE
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TC="${XDJ700_TOOLCHAIN:-$ROOT/toolchain/sh-sh4--uclibc--stable-2025.08-1/bin}"
CC="$TC/sh4-linux-gcc"
SRC="$ROOT/thirdparty/libflac-src/src/libFLAC"
OUT="${XDJ700_FLAC_BUILD_DIR:-$ROOT/thirdparty/build}"
# Preserve established -O2 archive identities by default. The explicit -Os
# profile makes a gate-enabled dual-format target payload fit the unchanged
# XIP tail. Build profiles must use separate XDJ700_FLAC_BUILD_DIR locations.
OPTIMIZATION=${XDJ700_LIBFLAC_OPTIMIZATION:--O2}
case "$OPTIMIZATION" in
  -O2|-Os) ;;
  *) echo "XDJ700_LIBFLAC_OPTIMIZATION must be -O2 or -Os" >&2; exit 2 ;;
esac
CFLAGS=("$OPTIMIZATION" -fstack-usage -fno-pie -fno-PIC -fno-builtin -ffunction-sections -fdata-sections -DNDEBUG -DHAVE_LROUND
 -DFLAC__HAS_OGG=0 -DHAVE_STDINT_H -DHAVE_INTTYPES_H -DSIZEOF_VOIDP=4
 -DXDJ700_FLAC_BLOCKSIZE_GUARD=1
 -DPACKAGE_VERSION='"1.4.3"'
 -I"$SRC" -I"$SRC/include" -I"$ROOT/thirdparty/libflac-src/include" -I"$ROOT/thirdparty/build")
mkdir -p "$OUT"
STAGE=$(mktemp -d "$OUT/.libflac-stage.XXXXXX")
cleanup() {
  case "${STAGE:-}" in
    "$OUT"/.libflac-stage.*)
      if [ -d "$STAGE" ]; then
        rm -rf -- "$STAGE"
      fi
      ;;
    *)
      echo "refusing to clean unexpected libFLAC stage: ${STAGE:-<unset>}" >&2
      ;;
  esac
}
trap cleanup EXIT HUP INT TERM
OBJS=()
for f in bitmath bitreader cpu crc fixed float format lpc md5 memory metadata_iterators metadata_object stream_decoder window; do
  "$CC" "${CFLAGS[@]}" -c "$SRC/$f.c" -o "$STAGE/$f.o"
  OBJS+=("$STAGE/$f.o")
done
# Keep the tracked build artifact reproducible; otherwise member timestamps
# make every probe run produce a meaningless archive diff.
"$TC/sh4-buildroot-linux-uclibc-ar" rcsD "$STAGE/libFLAC.a" "${OBJS[@]}"
python3 -B "$ROOT/tools/sh4_stack_usage.py" "$STAGE" "$TC/sh4-buildroot-linux-uclibc-ar"
"$CC" "${CFLAGS[@]}" -c "$ROOT/thirdparty/build/flac_glue.c" -o "$STAGE/flac_glue.o"
# Consumers must never observe an individual ar/object file while it is being
# written.  The stage lives below OUT, so each rename publishes one complete
# file on the same filesystem even when two local builders finish concurrently.
# This is not a multi-file transaction: the production dual builder compiles
# its own glue and links a stable private archive snapshot.  Legacy consumers
# of both shared outputs require separate serialization if their sources differ.
for object in "${OBJS[@]}"; do
  mv -f -- "$object" "$OUT/${object##*/}"
done
for report in "$STAGE"/*.su; do
  mv -f -- "$report" "$OUT/${report##*/}"
done
mv -f -- "$STAGE/flac_glue.o" "$OUT/flac_glue.o"
mv -f -- "$STAGE/libFLAC.a" "$OUT/libFLAC.a"
mv -f -- "$STAGE/libFLAC.stack.json" "$OUT/libFLAC.stack.json"
echo "== libFLAC.a built =="
"$TC/sh4-buildroot-linux-uclibc-size" "$OUT/libFLAC.a" | tail -2
