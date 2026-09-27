# Rebuild the FLAC alpha.1 template from source

This source build reproduces the exact `flac_alpha1_template.json` shipped with
the fixed XDJ-700 FLAC alpha.1 patcher. It uses the FLAC-only C sources, libFLAC
1.4.3 sources, sanitized SH-4 integration assembly, and the official v1.15
update supplied by the owner. It creates a sparse template and local review
artifacts. It does not create an update, write USB media, or contact a player.

Use an x86-64 Linux host with Python 3.10 or newer, Bash, and the exact Bootlin
SH-4/uClibc toolchain. Download the official
[`sh-sh4--uclibc--stable-2025.08-1.tar.xz`](https://toolchains.bootlin.com/downloads/releases/toolchains/sh-sh4/tarballs/sh-sh4--uclibc--stable-2025.08-1.tar.xz)
archive; its SHA-256 is
`e8c2065017a3cd2355dcbb12773edfa6b8078e963178f047ccdfa493ccef1f6a`.
Check Bootlin's [published checksum](https://toolchains.bootlin.com/downloads/releases/toolchains/sh-sh4/tarballs/sh-sh4--uclibc--stable-2025.08-1.sha256)
as well. Extract it outside this source tree and, if required after moving it,
run the included `relocate-sdk.sh`. The compiler is GCC 14.3.0 with binutils
2.43.1; the build script also pins the compiler launcher, archive and objcopy
tools, and `libgcc.a` by SHA-256. The toolchain is an external dependency and
is not redistributed here.

Keep the official update and all build output outside this source tree. The
official input must be an exact XDJ-700 v1.15 `XDJ700.UPD`: 17,371,335 bytes,
SHA-256 `73edec9802da51672257c2599efc04209dc92478fcbaa1a0425b3b122e33f99c`.
From this directory, run:

```sh
python3 -B build_flac_alpha1_source.py \
  --official-upd /path/to/official/XDJ700.UPD \
  --toolchain /path/to/sh-sh4--uclibc--stable-2025.08-1/bin \
  --output-dir /path/to/new/source-build.DO_NOT_FLASH
```

The output directory must be new and on a local disk. A successful build writes
`flac_alpha1_template.json` there: 80,481 bytes, SHA-256
`b54d831e8881eaab587115c9deadfccc69fbb41cdfdea39ae547f8ac25356462`.
This is byte-identical to the template in the alpha.1 patcher. The builder
also checks the compiled 53,228-byte FLAC payload, gate, satellite, cache,
complete decoded application, and all 236 generated template spans against
their reviewed identities. `SOURCE_BUILD_MANIFEST.json` pins source inputs.
The final `source-build-result.json` is written only after every check passes.

The assembly source uses zero placeholders at eight exact relocation windows.
During this local build, 98 instruction bytes are copied from the verified
owner-supplied official update and the two branch displacements are adjusted
for their new locations. No copied stock instruction values are included in
the source package. The three `alac_*.h` files provide shared type declarations
needed to compile retained interfaces; this build includes no ALAC C decoder
or ALAC payload mode.

The source build covers the fixed alpha.1 application template. The separate
`patcher.py` consumes that template and the same official input to serialize
the v1.16 update. Its full test checks the 17,479,419-byte output against SHA-256
`8814d02f13e8d7a9feaa8bb6f45a11fccd174d144737cc009b087ae7b5c089fb`.
No change to the existing alpha.1 firmware or playback behavior is implied by
adding these source build files.

## Patcher verification

The patcher rejects changed or unknown firmware, a modified template, and an
existing output path. It reconstructs the official MAIN/PANL sections, applies
the sparse template, fills the owner-copy windows from the verified input,
and regenerates compression, section checksums, S-records and document CRCs.
It verifies the complete output against the size and hash above before
publishing it atomically from a temporary local file.

Run the complete patcher test suite with your official input:

```sh
python3 -B tests/test_patcher.py --official-upd /path/to/official/XDJ700.UPD --full
```

The tests cover exact reconstruction and refusal cases using temporary local
files. They do not access USB media or a player. Keep firmware and generated
output outside this repository.
