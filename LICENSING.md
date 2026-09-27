# Licensing

Project-authored code (source and compiled portions), documentation, build tools,
tests, and metadata use [MIT](LICENSE): Copyright (c) 2026 DeckVolve contributors.
Contributors retain their respective rights; this name is collective attribution.

## Included dependencies

| Component | License and notices |
| --- | --- |
| Selected libFLAC 1.4.3 sources in `thirdparty/libflac-src/` | BSD-3-Clause; see [COPYING.Xiph](COPYING.Xiph). |
| libFLAC `md5.c` and `include/private/md5.h` | Their file-specific public-domain notices; see [MD5_NOTICES.txt](MD5_NOTICES.txt). |
| GCC 14.3 runtime portions in the compiled template | GPL-3.0-or-later WITH GCC-exception-3.1; see [COPYING3](COPYING3) and [COPYING.RUNTIME](COPYING.RUNTIME). |

The remaining project sources in `thirdparty/` use MIT, including the shared
`alac_*.h` type headers. Those headers do not include an ALAC decoder.

The only modified upstream libFLAC file is `src/libFLAC/stream_decoder.c`.
Its upstream material remains BSD-3-Clause; the project's additions use MIT
(`BSD-3-Clause AND MIT` for the combined file). The upstream archive and local
change are recorded in [LIBFLAC.json](provenance/LIBFLAC.json) and
[stream_decoder.local.patch](provenance/stream_decoder.local.patch).

The compiled template contains project code and the dependency portions above;
the MIT license does not replace their terms. GCC runtime portions were built
using GCC under the Runtime Library Exception's Eligible Compilation Process.
The compiler and standalone runtime archive are not distributed here.
[TOOLCHAIN.json](provenance/TOOLCHAIN.json) identifies the toolchain, runtime
archive, and upstream source and license downloads.

## Official firmware

Official firmware is not included or covered by this project's MIT license.
The source and sparse template use zero placeholders for eight relocated
instruction windows. The patcher and source builder fill them locally from
the user's verified official firmware.

These licenses do not grant rights to redistribute the official firmware or
the complete generated updater.
