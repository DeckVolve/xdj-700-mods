#!/usr/bin/env python3
"""Build a neutral retained FLAC/ALAC payload for the SH-4 target.

The linked one-shot entry and exported incremental open/next/close entries
accept a caller-provided random-access reader and synchronous PCM sink. They
are accompanied by the retained virtual-WAVE bridge, stock-preserving
public-format router, and dormant XDJ-700 adapter/trampoline exports.  This
tool does not install the required call-site redirects, patch or package an
update image, or make a hardware-flashability claim.
The existing tools/build_flac_payload.py remains the separate FLAC-only
default builder and is not imported by this additive tool.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zlib

if __package__:
    from .sh4_stack_usage import audit_linked, load_archive_report, parse_report
else:
    from sh4_stack_usage import audit_linked, load_archive_report, parse_report


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TOOLCHAIN = ROOT / "toolchain/sh-sh4--uclibc--stable-2025.08-1/bin"
DEFAULT_BUILD_DIR = ROOT / "thirdparty/build"

# The target probe models the SH-4 P2 XIP alias as physical offsets below
# 0x00700000.  Keep this exclusive end and the flash-tail start together so
# the flat payload cannot grow beyond the address range the runner can map.
DEFAULT_XIP_ALIAS_BASE = 0xA0000000
DEFAULT_XIP_PHYS_LIMIT = 0x00700000
DEFAULT_FLASH_TAIL_OFFSET = 0x006EA7C0
DEFAULT_XIP_VMA_LIMIT = DEFAULT_XIP_ALIAS_BASE + DEFAULT_XIP_PHYS_LIMIT
DEFAULT_CODE_VMA = DEFAULT_XIP_ALIAS_BASE + DEFAULT_FLASH_TAIL_OFFSET
DEFAULT_MAX_PAYLOAD_BYTES = DEFAULT_XIP_PHYS_LIMIT - DEFAULT_FLASH_TAIL_OFFSET
DEFAULT_BSS_VMA = 0x0B800000
DEFAULT_MAX_BSS_BYTES = 0x00180000
FLAC_CALLER_ARENA_BYTES = 128 * 1024
# This guards compiler-reported static frames in dual_format_payload.c.  It
# does not claim a complete device call-chain high-water bound.
SH4_CODEC_STACK_FRAME_LIMIT = 640
GATE_DISPATCH_RETURN_PR = {
    False: 0x080FBA9C,
    True: 0x080FBAA0,
}
GATE_HOOK_RETURN_PR = {
    False: 0x080FBA02,
    True: 0x080FBA06,
}
FLAT_OUTPUT_SECTIONS = (".text", ".rodata", ".data", ".got", ".got.extra")
BSS_OUTPUT_SECTIONS = (".bss",)
DIAGNOSTIC_FIXTURES = {
    "flac": (1, "stock_flac_full_lifecycle_fixture.flac", 6135,
             "d45df9804396da6448beaf5b339261e0f779a1e96076a289f0862ef78dad53ea"),
    "alac": (2, "alac_stereo_44100_16_fixture.m4a", 3527,
             "2f32adaf8957d42b058fbe6f6addfcfbbae7cc124572e9e4a01b4c5e96b94903"),
    "flac-audible": (1, "audible_stereo_44100_16_3s.flac", 44872,
                     "dc40b919b53861f85e55899cc5d564e52195318f0e2f345325278d3f4a66fb54"),
    "alac-audible": (2, "audible_stereo_44100_16_3s.m4a", 64050,
                      "b8619626d3248e98883a3fb456d5e254690e971ee8589b9e8b791d9da34253e7"),
    "alac-audible-800ms": (2, "audible_stereo_44100_16_800ms.m4a", 17282,
                            "80fb9cb6c64e2c3d26d98e0b87192782f3173d082e8abeb84fb515c350664cde"),
}
DIAGNOSTIC_CALLERS = {
    "helper": (0x080FBF00, 0x080FC000),
    "completion": (0x080FC100, 0x080FC200),
    "finalizer": (0x080FD100, 0x080FD2E0),
}
DIAGNOSTIC_HANDLERS = (
    "stock_lossless_non_audio_helper_handler",
    "stock_lossless_non_audio_completion_handler",
    "stock_lossless_non_audio_finalizer",
)

SECTION_LINE = re.compile(
    r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+"
    r"([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+"
    r"([0-9A-Fa-f]+)\s+(\S+)\s+\d+\s+\d+\s+\d+\s*$"
)


def run(
    argv: list[Path | str], *, capture: bool = False,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(str(part) for part in argv))
    return subprocess.run(
        [str(part) for part in argv], cwd=ROOT, check=True, text=True,
        capture_output=capture, env=env
    )


def snapshot_stable_file(source: Path, destination: Path) -> Path:
    """Publish one private snapshot, rejecting a source changed mid-copy."""

    before = source.read_bytes()
    destination.write_bytes(before)
    if source.read_bytes() != before:
        raise SystemExit(f"{source.name} changed while it was snapshotted")
    return destination


def write_linker_script(path: Path, code_vma: int, bss_vma: int,
                        code_section_subalignment: int | None = None,
                        diagnostic: bool = False) -> None:
    subalign = (
        f" SUBALIGN({code_section_subalignment})"
        if code_section_subalignment is not None else ""
    )
    diagnostic_text = "" if not diagnostic else "\n".join(
        f"    KEEP(*(.text.{name}))" for name in DIAGNOSTIC_HANDLERS
    ) + "\n"
    diagnostic_rodata = "" if not diagnostic else (
        "    KEEP(*(.rodata.stock_lossless_non_audio_policy))\n"
        "    KEEP(*(.rodata.xdj700_non_audio_fixture))\n"
    )
    path.write_text(
        f'''OUTPUT_FORMAT("elf32-sh-linux")
OUTPUT_ARCH(sh)
ENTRY(dual_format_payload_decode)

SECTIONS
{{
  . = 0x{code_vma:08X};
  .text : ALIGN(4){subalign}
  {{
    KEEP(*(.text.dual_format_payload_decode))
    KEEP(*(.text.dual_format_payload_bind_shared_storage))
    KEEP(*(.text.dual_format_payload_session_open))
    KEEP(*(.text.dual_format_payload_session_next))
    KEEP(*(.text.dual_format_payload_session_close))
    KEEP(*(.text.lossless_wave_bridge_open))
    KEEP(*(.text.lossless_wave_bridge_size))
    KEEP(*(.text.lossless_wave_bridge_read_at))
    KEEP(*(.text.lossless_wave_bridge_close))
    KEEP(*(.text.retained_format_router_classify))
    KEEP(*(.text.stock_lossless_dispatch_trampoline))
    KEEP(*(.text.stock_lossless_read_entry))
    KEEP(*(.text.stock_lossless_read_original))
    KEEP(*(.text.stock_lossless_adapter_dispatch))
    KEEP(*(.text.stock_lossless_adapter_dispatch_gate_impl))
    KEEP(*(.text.stock_lossless_adapter_read))
    KEEP(*(.text.stock_lossless_adapter_size))
    KEEP(*(.text.stock_lossless_adapter_position))
    KEEP(*(.text.stock_lossless_adapter_seek))
    KEEP(*(.text.stock_lossless_adapter_eof))
    KEEP(*(.text.stock_lossless_adapter_transfer))
    KEEP(*(.text.stock_lossless_adapter_close))
{diagnostic_text}    *(.text .text.*)
  }}
  .rodata (READONLY) : ALIGN(4)
  {{
    KEEP(*(.rodata.stock_lossless_integrity_gate_policy))
    KEEP(*(.rodata.stock_lossless_trial_policy))
{diagnostic_rodata}    *(.rodata .rodata.* .srodata .srodata.*)
    *(.data.rel.ro .data.rel.ro.*)
  }}
  .data : ALIGN(4)
  {{
    *(.data .data.* .sdata .sdata.*)
  }}
  /* SH R_SH_GOTPC/R_SH_GOT32 relocations require the target linker\'s
     canonical single-GOT layout: the reserved .got.plt words establish
     _GLOBAL_OFFSET_TABLE_, followed by ordinary .got slots.  Splitting or
     reversing these input sections makes libgcc PIC helpers address the
     reserved zero word instead of their resolved slot. */
  ASSERT(SIZEOF(.data) == 0, "XIP payload must not contain writable data")
  .got (READONLY) : ALIGN(4)
  {{
    *(.got.plt) *(.igot.plt)
    *(.got) *(.igot)
  }}
  .got.extra (READONLY) : ALIGN(4) {{ *(.got.* .igot.*) }}
  __payload_end = .;

  /* Runtime-only storage is deliberately excluded from the flat binary. */
  . = 0x{bss_vma:08X};
  __bss_start = .;
  .bss (NOLOAD) : ALIGN(8)
  {{
    *(.bss .bss.* .sbss .sbss.*)
    *(COMMON)
  }}
  __bss_end = .;
}}
''',
        encoding="ascii",
    )


def symbol_value(nm_output: str, wanted: str) -> int | None:
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-1] == wanted:
            try:
                return int(fields[0], 16)
            except ValueError:
                return None
    return None


def readelf_symbol(readelf_output: str, wanted: str) -> dict[str, int | str] | None:
    for line in readelf_output.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[-1] == wanted:
            try:
                return {
                    "value": int(fields[1], 16),
                    "size": int(fields[2], 10),
                    "type": fields[3],
                    "bind": fields[4],
                    "visibility": fields[5],
                    "section": fields[6],
                }
            except ValueError:
                return None
    return None


def parse_stack_usage(path: Path) -> dict[str, int]:
    """Read GCC -fstack-usage output, rejecting dynamic or malformed rows."""
    usage: dict[str, int] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            raise SystemExit(f"malformed stack-usage row: {line!r}")
        location, byte_text, qualifier = fields
        function = location.rsplit(":", 1)[-1]
        try:
            byte_count = int(byte_text, 10)
        except ValueError as error:
            raise SystemExit(f"invalid stack usage for {function}: {byte_text}") from error
        if qualifier != "static":
            raise SystemExit(
                f"unbounded stack usage for {function}: {qualifier}"
            )
        usage[function] = byte_count
    if not usage:
        raise SystemExit("compiler emitted no stack-usage records")
    return usage


def parse_sections(readelf_output: str) -> list[dict[str, int | str]]:
    sections: list[dict[str, int | str]] = []
    for line in readelf_output.splitlines():
        match = SECTION_LINE.match(line)
        if not match:
            continue
        name, section_type, address, offset, size, _entry_size, flags = match.groups()
        sections.append({
            "name": name, "type": section_type, "address": int(address, 16),
            "offset": int(offset, 16), "size": int(size, 16), "flags": flags,
        })
    if not sections:
        raise SystemExit("readelf did not report section headers")
    return sections


def validate_layout(elf: Path, readelf: Path, *, code_vma: int,
                    payload_end: int, bss_vma: int, bss_start: int, bss_end: int,
                    max_bss_bytes: int, require_bss: bool = True,
                    xip_vma_limit: int = DEFAULT_XIP_VMA_LIMIT) -> None:
    sections = parse_sections(run([readelf, "-W", "-S", elf], capture=True).stdout)
    flat_sections: list[tuple[str, int, int]] = []
    bss_sections: list[tuple[str, int, int]] = []
    unexpected: list[str] = []
    for section in sections:
        if not section["size"] or "A" not in section["flags"]:
            continue
        name, section_type = str(section["name"]), str(section["type"])
        address, size = int(section["address"]), int(section["size"])
        end = address + size
        if end < address:
            raise SystemExit(f"allocatable section wraps address space: {name}")
        if section_type == "NOBITS":
            if name not in BSS_OUTPUT_SECTIONS:
                unexpected.append(f"{name} (NOBITS)")
            else:
                bss_sections.append((name, address, end))
        elif name not in FLAT_OUTPUT_SECTIONS:
            unexpected.append(f"{name} ({section_type})")
        else:
            if "W" in str(section["flags"]):
                raise SystemExit(
                    f"XIP payload section {name} is unexpectedly writable"
                )
            flat_sections.append((name, address, end))
    if unexpected:
        raise SystemExit("unsupported allocatable section(s): " + ", ".join(unexpected))
    if not flat_sections:
        raise SystemExit("linked payload has no file-backed allocatable sections")
    if min(address for _name, address, _end in flat_sections) != code_vma:
        raise SystemExit("flat payload does not start at the requested code VMA")
    if max(end for _name, _address, end in flat_sections) != payload_end:
        raise SystemExit("__payload_end does not end the file-backed payload")
    if payload_end <= code_vma:
        raise SystemExit("flat payload has an invalid or empty address range")
    if payload_end > xip_vma_limit:
        raise SystemExit(
            f"flat payload ends at 0x{payload_end:08X}, beyond exclusive "
            f"XIP limit 0x{xip_vma_limit:08X}"
        )
    if bss_start != bss_vma or bss_end < bss_start:
        raise SystemExit("linked BSS does not match its requested range")
    if require_bss and bss_end == bss_start:
        raise SystemExit("linked BSS does not match its requested nonempty range")
    if bss_end - bss_start > max_bss_bytes:
        raise SystemExit(f"BSS is {bss_end - bss_start} bytes, over limit {max_bss_bytes}")
    if code_vma < bss_end and bss_start < payload_end:
        raise SystemExit("flat payload overlaps runtime BSS")
    for name, address, end in bss_sections:
        if address < bss_start or end > bss_end:
            raise SystemExit(f"BSS section {name} lies outside the declared BSS range")
    program_headers = run([readelf, "-W", "-l", elf], capture=True).stdout
    for line in program_headers.splitlines():
        if line.lstrip().startswith("LOAD") and re.search(
            r"\bW\b.*\bE\b|\bE\b.*\bW\b", line
        ):
            raise SystemExit(
                f"XIP payload has a writable/executable LOAD segment: {line.strip()}"
            )


def validate_static_sh_got(output: Path, elf: Path, readelf: Path,
                           nm_output: str, *, code_vma: int,
                           payload_end: int) -> tuple[int, int, int]:
    """Reject a split SH GOT and prove libgcc's __clz_tab slot is resolved."""
    sections = parse_sections(run([readelf, "-W", "-S", elf], capture=True).stdout)
    got_sections = [section for section in sections
                    if section["name"] == ".got" and section["size"]]
    if len(got_sections) != 1:
        raise SystemExit("linked payload must contain exactly one nonempty .got section")
    if any(section["name"] == ".got.plt" and section["size"]
           for section in sections):
        raise SystemExit(
            "SH payload has a split .got.plt output section; R_SH_GOT32 "
            "offsets require the canonical combined GOT"
        )

    got_base = symbol_value(nm_output, "_GLOBAL_OFFSET_TABLE_")
    clz_table = symbol_value(nm_output, "__clz_tab")
    udivdi3 = symbol_value(nm_output, "__udivdi3")
    if None in (got_base, clz_table, udivdi3):
        raise SystemExit(
            "linked ALAC payload is missing _GLOBAL_OFFSET_TABLE_, "
            "__clz_tab, or __udivdi3"
        )
    assert got_base is not None and clz_table is not None and udivdi3 is not None

    got = got_sections[0]
    got_address, got_size = int(got["address"]), int(got["size"])
    if got_base != got_address:
        raise SystemExit(
            f"SH GOT base is 0x{got_base:08X}, but combined .got starts at "
            f"0x{got_address:08X}"
        )
    if got_address < code_vma or got_address + got_size > payload_end:
        raise SystemExit("combined SH GOT lies outside the flat payload")
    if not code_vma <= clz_table < payload_end or not code_vma <= udivdi3 < payload_end:
        raise SystemExit("__clz_tab or __udivdi3 lies outside the flat payload")

    flat = output.read_bytes()
    got_offset = got_address - code_vma
    got_bytes = flat[got_offset:got_offset + got_size]
    encoded_clz = clz_table.to_bytes(4, "little")
    slot_offset = got_bytes.find(encoded_clz)
    if slot_offset < 0 or slot_offset & 3:
        raise SystemExit(
            "combined SH GOT does not contain an aligned resolved __clz_tab slot"
        )
    if got_bytes.find(encoded_clz, slot_offset + 1) >= 0:
        raise SystemExit("combined SH GOT contains ambiguous __clz_tab slots")
    return got_base, got_address + slot_offset, clz_table


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/opencode/xdj700_dual_format_payload.bin"))
    parser.add_argument("--elf", type=Path, default=Path("/tmp/opencode/xdj700_dual_format_payload.elf"))
    parser.add_argument("--map", dest="map_path", type=Path,
                        default=Path("/tmp/opencode/xdj700_dual_format_payload.map"))
    parser.add_argument("--code-vma", type=lambda value: int(value, 0), default=DEFAULT_CODE_VMA)
    parser.add_argument(
        "--xip-vma-limit", "--code-vma-limit", dest="xip_vma_limit",
        type=lambda value: int(value, 0), default=DEFAULT_XIP_VMA_LIMIT,
        help=(
            "exclusive code-image end address; --xip-vma-limit is retained "
            "as the backward-compatible spelling (default: declared "
            "flash-tail limit)"
        ),
    )
    parser.add_argument("--bss-vma", type=lambda value: int(value, 0), default=DEFAULT_BSS_VMA)
    parser.add_argument(
        "--code-section-subalignment", type=lambda value: int(value, 0),
        help=(
            "override input text-section alignment in the generated linker "
            "script; omitted by default to preserve the established XIP layout"
        ),
    )
    parser.add_argument("--max-bss-bytes", type=lambda value: int(value, 0),
                        default=DEFAULT_MAX_BSS_BYTES)
    parser.add_argument("--toolchain", type=Path,
                        help="SH-4 toolchain bin directory (default: XDJ700_TOOLCHAIN or the bundled toolchain)")
    parser.add_argument("--build-dir", type=Path,
                        help="libFLAC artifact directory (default: XDJ700_FLAC_BUILD_DIR or thirdparty/build)")
    parser.add_argument(
        "--glue", type=Path, help=argparse.SUPPRESS,
    )
    parser.add_argument("--libflac", type=Path,
                        help="prebuilt libFLAC.a (default: selected build directory)")
    parser.add_argument("--skip-build", action="store_true",
                        help="reuse existing FLAC artifacts instead of rebuilding them")
    parser.add_argument(
        "--print-default-layout", action="store_true",
        help="print code VMA, flash-tail offset, XIP limit, and max payload bytes",
    )
    parser.add_argument(
        "--target-runtime", action="store_true",
        help="use the stock-adapter runtime provider and require zero linked BSS",
    )
    parser.add_argument(
        "--test-stock-fifo-bytes", type=lambda value: int(value, 0),
        choices=(18_432, 27_648), help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--require-stock-integrity-gate", action="store_true",
        help=(
            "compile the stock adapter so only an integrity gate's exact "
            "GATE_READY+WRITER handoff can enter dispatch"
        ),
    )
    parser.add_argument(
        "--disable-stock-alac",
        action="store_true",
        help=(
            "compile the stock-lossless adapter so it does not divert ALAC/M4A; "
            "the public ALAC decoder exports remain linked for non-stock tests"
        ),
    )
    parser.add_argument(
        "--flac-code-only", action="store_true",
        help=(
            "omit ALAC codec/container objects and use FLAC-only audio and "
            "routing implementations; requires gated target runtime with "
            "--disable-stock-alac"
        ),
    )
    parser.add_argument(
        "--enable-stock-alac",
        action="store_true",
        help=(
            "compile the stock-lossless adapter so it may divert eligible "
            "ALAC/M4A inputs; this is an explicit diagnostic-only opt-in"
        ),
    )
    parser.add_argument(
        "--stock-alac-selector6-only", action="store_true",
        help="paired research policy: admit stock ALAC only from observed selector 6",
    )
    parser.add_argument(
        "--research-alac-native-fault-record", action="store_true",
        help="link the default-off ALAC first-fault recorder for private payload-only stack research",
    )
    parser.add_argument("--non-audio-diagnostic", choices=tuple(DIAGNOSTIC_FIXTURES),
                        help="link the quarantined exact-fixture diagnostic adapter")
    parser.add_argument(
        "--trial-exact-fixture", choices=tuple(DIAGNOSTIC_FIXTURES),
        help=(
            "restrict a non-diagnostic stock-WAVE handoff trial to one exact "
            "public fixture without linking diagnostic helper/completion handlers"
        ),
    )
    parser.add_argument(
        "--non-audio-decoder-drain", action="store_true",
        help=(
            "for an exact-fixture diagnostic, decode and fingerprint locally "
            "then return terminal without publishing selector 11"
        ),
    )
    parser.add_argument(
        "--non-audio-open-only", action="store_true",
        help=(
            "for the exact ALAC diagnostic fixture, stop after bounded bridge "
            "open and checked cleanup without PCM or selector 11"
        ),
    )
    for caller in DIAGNOSTIC_CALLERS:
        parser.add_argument(f"--non-audio-{caller}-return-pr",
                            type=lambda value: int(value, 0))
    return parser.parse_args()


def checked_u32(value: int, label: str) -> None:
    if value < 0 or value > 0xFFFFFFFF:
        raise SystemExit(f"{label} is outside the 32-bit SH-4 address space")


def main() -> int:
    args = parse_args()
    if args.research_alac_native_fault_record:
        if not (args.target_runtime and args.require_stock_integrity_gate and
                args.enable_stock_alac and args.stock_alac_selector6_only and
                not args.disable_stock_alac and not args.non_audio_diagnostic and
                not args.trial_exact_fixture):
            raise SystemExit(
                "native-fault research requires gated target-runtime ALAC selector 6 "
                "without diagnostic or exact-fixture modes"
            )
        for path in (args.output, args.elf, args.map_path):
            if ".DO_NOT_FLASH." not in path.name:
                raise SystemExit(
                    "native-fault research outputs must each contain .DO_NOT_FLASH."
                )
    if args.disable_stock_alac and args.enable_stock_alac:
        raise SystemExit("--disable-stock-alac and --enable-stock-alac conflict")
    if args.flac_code_only and not (
        args.disable_stock_alac and args.target_runtime and
        args.require_stock_integrity_gate and not args.non_audio_diagnostic and
        not args.trial_exact_fixture
    ):
        raise SystemExit(
            "--flac-code-only requires --disable-stock-alac, --target-runtime, "
            "--require-stock-integrity-gate, and no fixture diagnostic"
        )
    if args.stock_alac_selector6_only and not (
        args.target_runtime and args.require_stock_integrity_gate and
        args.enable_stock_alac and not args.non_audio_diagnostic and
        not args.trial_exact_fixture
    ):
        raise SystemExit(
            "--stock-alac-selector6-only requires gated target-runtime ALAC "
            "without diagnostic or exact-fixture modes"
        )
    if args.require_stock_integrity_gate and not args.target_runtime:
        raise SystemExit("--require-stock-integrity-gate requires --target-runtime")
    if args.test_stock_fifo_bytes is not None:
        if not args.target_runtime:
            raise SystemExit("--test-stock-fifo-bytes requires --target-runtime")
        if (args.require_stock_integrity_gate or args.non_audio_diagnostic or
                args.trial_exact_fixture):
            raise SystemExit(
                "--test-stock-fifo-bytes is incompatible with package, "
                "diagnostic, and exact-trial modes"
            )
    if args.require_stock_integrity_gate and not (
            args.disable_stock_alac or args.enable_stock_alac):
        raise SystemExit(
            "--require-stock-integrity-gate requires an explicit "
            "--disable-stock-alac or --enable-stock-alac mode"
        )
    diagnostic_policy = {}
    for caller, (start, end) in DIAGNOSTIC_CALLERS.items():
        value = getattr(args, f"non_audio_{caller}_return_pr")
        if args.non_audio_diagnostic:
            if value is None or not start < value < end or value & 1:
                raise SystemExit(f"diagnostic {caller} requires an aligned exact return PR inside its cave")
            diagnostic_policy[f"stock_lossless_non_audio_{caller}_return_pr"] = value
        elif value is not None:
            raise SystemExit("non-audio return PR requires --non-audio-diagnostic")
    fixture_bytes = b""
    if args.trial_exact_fixture and args.non_audio_diagnostic:
        raise SystemExit("trial exact-fixture and non-audio diagnostic modes conflict")
    if args.non_audio_decoder_drain and not args.non_audio_diagnostic:
        raise SystemExit(
            "--non-audio-decoder-drain requires --non-audio-diagnostic"
        )
    if args.non_audio_open_only and (
            args.non_audio_diagnostic not in (
                "alac", "alac-audible", "alac-audible-800ms"
            ) or args.non_audio_decoder_drain):
        raise SystemExit(
            "--non-audio-open-only requires an ALAC diagnostic without decoder drain"
        )
    if args.non_audio_diagnostic:
        if not args.target_runtime or not args.require_stock_integrity_gate:
            raise SystemExit("non-audio diagnostic requires target runtime and stock integrity gate")
        mode, filename, size, digest = DIAGNOSTIC_FIXTURES[args.non_audio_diagnostic]
        if (mode == 2) != args.enable_stock_alac:
            raise SystemExit("non-audio diagnostic fixture must match explicit stock ALAC mode")
        fixture_bytes = (ROOT / "thirdparty" / filename).read_bytes()
        if len(fixture_bytes) != size or hashlib.sha256(fixture_bytes).hexdigest() != digest:
            raise SystemExit("non-audio diagnostic fixture identity mismatch")
        diagnostic_policy["stock_lossless_non_audio_diagnostic_mode"] = mode
        diagnostic_policy["stock_lossless_non_audio_decoder_drain"] = int(
            args.non_audio_decoder_drain
        )
        if args.non_audio_open_only:
            diagnostic_policy["stock_lossless_non_audio_open_only"] = 1
    if args.trial_exact_fixture:
        if not args.target_runtime or not args.require_stock_integrity_gate:
            raise SystemExit("trial exact fixture requires target runtime and stock integrity gate")
        mode, filename, size, digest = DIAGNOSTIC_FIXTURES[args.trial_exact_fixture]
        if (mode == 2) != args.enable_stock_alac:
            raise SystemExit("trial exact fixture must match explicit stock ALAC mode")
        fixture_bytes = (ROOT / "thirdparty" / filename).read_bytes()
        if len(fixture_bytes) != size or hashlib.sha256(fixture_bytes).hexdigest() != digest:
            raise SystemExit("trial exact fixture identity mismatch")
    if args.print_default_layout:
        print(
            f"0x{DEFAULT_CODE_VMA:08X} 0x{DEFAULT_FLASH_TAIL_OFFSET:08X} "
            f"0x{DEFAULT_XIP_VMA_LIMIT:08X} {DEFAULT_MAX_PAYLOAD_BYTES}"
        )
        return 0
    checked_u32(args.code_vma, "code VMA")
    checked_u32(args.xip_vma_limit, "XIP VMA limit")
    checked_u32(args.bss_vma, "BSS VMA")
    if args.code_vma & 3:
        raise SystemExit("code VMA must be 4-byte aligned")
    if args.xip_vma_limit <= args.code_vma:
        raise SystemExit("code VMA limit must be above the code VMA")
    if args.xip_vma_limit & 3:
        raise SystemExit("code VMA limit must be 4-byte aligned")
    if args.bss_vma & 7:
        raise SystemExit("BSS VMA must be 8-byte aligned")
    if (
        args.code_section_subalignment is not None
        and (
            args.code_section_subalignment < 4
            or args.code_section_subalignment & (args.code_section_subalignment - 1)
        )
    ):
        raise SystemExit("code section subalignment must be a power of two >= 4")
    if args.max_bss_bytes <= 0 or args.bss_vma > 0xFFFFFFFF - args.max_bss_bytes:
        raise SystemExit("max BSS range is invalid")

    toolchain = (args.toolchain or
                 Path(os.environ.get("XDJ700_TOOLCHAIN", DEFAULT_TOOLCHAIN))).resolve()
    build_dir = (args.build_dir or
                 Path(os.environ.get("XDJ700_FLAC_BUILD_DIR", DEFAULT_BUILD_DIR))).resolve()
    archive = (args.libflac or build_dir / "libFLAC.a").resolve()
    if args.glue is not None:
        raise SystemExit(
            "--glue is not supported: the dual-format builder compiles "
            "flac_glue.c with FLAC_MOD_REQUIRE_CALLER_ARENA=1"
        )
    build_env = os.environ.copy()
    build_env["XDJ700_TOOLCHAIN"] = str(toolchain)
    build_env["XDJ700_FLAC_BUILD_DIR"] = str(build_dir)
    if not args.skip_build:
        run([ROOT / "tools/build_libflac_sh4.sh"], env=build_env)
    missing = [path for path in (archive,) if not path.is_file()]
    if missing:
        raise SystemExit("missing SH-4 codec artifact(s): " + ", ".join(map(str, missing)) +
                         "; build them with tools/build_libflac_sh4.sh")
    output, elf, map_path = args.output.resolve(), args.elf.resolve(), args.map_path.resolve()
    for path in (output, elf, map_path):
        path.parent.mkdir(parents=True, exist_ok=True)

    source_files = (
        ROOT / "thirdparty/build/dual_format_payload.c",
        ROOT / "thirdparty/audio_file.c",
    )
    if not args.flac_code_only:
        source_files += (
            ROOT / "thirdparty/alac_mod.c",
            ROOT / "thirdparty/alac_container.c",
            ROOT / "thirdparty/alac_stream.c",
        )
    source_files += (
        ROOT / "thirdparty/flac_metadata.c",
        ROOT / "thirdparty/lossless_wave_bridge.c",
        ROOT / "thirdparty/retained_format_router_flac_only.c"
        if args.flac_code_only else
        ROOT / "thirdparty/retained_format_router.c",
        ROOT / "thirdparty/stock_lossless_adapter.c",
    )
    if args.research_alac_native_fault_record:
        source_files += (ROOT / "thirdparty/alac_native_fault_record.c",)
    diagnostic_sources = {
        "xdj700_non_audio_diagnostic.c", "stock_lossless_non_audio_runtime.c",
    }
    if args.non_audio_diagnostic:
        source_files += tuple(ROOT / "thirdparty" / name for name in sorted(diagnostic_sources))
    if args.non_audio_diagnostic or args.trial_exact_fixture:
        source_files += (ROOT / "thirdparty/xdj700_non_audio_fixture.c",)
    payload_stack_usage: dict[str, int] = {}
    all_stack_units: dict[str, dict[str, int]] = {}
    with tempfile.TemporaryDirectory(prefix="xdj700-dual-format-link-") as temp_name:
        temp = Path(temp_name)
        # Link only a private, byte-pinned archive snapshot.  A shared
        # build/libFLAC.a can otherwise be replaced in place by another local
        # build between validation and the linker read, making --skip-build
        # nondeterministic without leaving an attributable input identity.
        link_archive = snapshot_stable_file(archive, temp / "libFLAC.a")
        link_archive_sha256 = hashlib.sha256(link_archive.read_bytes()).hexdigest()
        report_path = archive.with_name("libFLAC.stack.json")
        if not report_path.is_file():
            raise SystemExit("libFLAC stack evidence missing; rebuild with tools/build_libflac_sh4.sh")
        link_report = snapshot_stable_file(report_path, temp / "libFLAC.stack.json")
        all_stack_units.update(load_archive_report(
            link_archive, link_report, toolchain / "sh4-buildroot-linux-uclibc-ar"))
        objects: list[Path] = []
        compile_flags: list[Path | str] = [
            toolchain / "sh4-linux-gcc", "-O2", "-std=c99", "-fno-pie", "-fno-PIC", "-fno-builtin",
            "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-I", ROOT / "thirdparty",
            "-I", ROOT / "thirdparty/build",
            f"-DFLAC_MOD_ARENA_SIZE={FLAC_CALLER_ARENA_BYTES}",
        ]
        if args.target_runtime:
            compile_flags.append("-DXDJ700_TARGET_RUNTIME_PROVIDER=1")
        if args.non_audio_diagnostic:
            compile_flags.append("-DXDJ700_NON_AUDIO_DIAGNOSTIC_ENABLE=1")
            compile_flags.extend(f"-D{name.upper()}=0x{value:08X}"
                                 for name, value in diagnostic_policy.items())
        if args.non_audio_diagnostic or args.trial_exact_fixture:
            crc = zlib.crc32(fixture_bytes) & 0xFFFFFFFF
            prefix = "ALAC" if mode == 2 else "FLAC"
            compile_flags.extend([
                f"-DXDJ700_NAF_{prefix}_BYTES={size}u",
                f"-DXDJ700_NAF_{prefix}_CRC32=0x{crc:08X}u",
                f"-DXDJ700_NAF_MAX_READS={(size + 63) // 64}u",
            ])
        if args.trial_exact_fixture:
            compile_flags.append(
                f"-DSTOCK_LOSSLESS_TRIAL_EXACT_FIXTURE_MODE={mode}"
            )
        if args.require_stock_integrity_gate:
            compile_flags.extend([
                "-DSTOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE=1",
                "-DSTOCK_LOSSLESS_GATE_HOOK_RETURN_PR="
                f"0x{GATE_HOOK_RETURN_PR[args.enable_stock_alac]:08X}",
            ])
        if args.disable_stock_alac:
            compile_flags.append("-DSTOCK_LOSSLESS_ENABLE_ALAC=0")
        elif args.enable_stock_alac:
            compile_flags.append("-DSTOCK_LOSSLESS_ENABLE_ALAC=1")
        if args.flac_code_only:
            compile_flags.append("-DXDJ700_FLAC_ONLY_PAYLOAD=1")
        if args.stock_alac_selector6_only:
            compile_flags.append("-DSTOCK_LOSSLESS_ALAC_SELECTOR6_ONLY=1")
        for source in source_files:
            object_path = temp / (source.stem + ".o")
            source_flags: list[str] = []
            if source.name in diagnostic_sources or source.name == "xdj700_non_audio_fixture.c" or source.name in {
                "dual_format_payload.c",
                "lossless_wave_bridge.c",
                "retained_format_router.c",
                "retained_format_router_flac_only.c",
                "stock_lossless_adapter.c",
            }:
                source_flags = [
                    "-Wall", "-Wextra", "-Werror", "-fstack-usage",
                    f"-Wframe-larger-than={SH4_CODEC_STACK_FRAME_LIMIT}",
                ]
            if source.name in {"stock_lossless_adapter.c",
                               "stock_lossless_non_audio_runtime.c",
                               "xdj700_non_audio_diagnostic.c"}:
                # The permanent lifecycle word uses SH-4A linked-load /
                # conditional-store atomics.  This model emits MOVLI.L and
                # MOVCO.L directly and does not depend on an RTOS gUSA handler.
                source_flags.extend([
                    "-m4a", "-ml", "-matomic-model=hard-llcs,strict",
                ])
            if source.name == "stock_lossless_adapter.c" and args.research_alac_native_fault_record:
                source_flags.append("-DSTOCK_LOSSLESS_ALAC_NATIVE_FAULT_RECORD=1")
            if source.name == "alac_native_fault_record.c":
                # Its shared sync header also defines adapter-only static helpers.
                source_flags.append("-Wno-unused-function")
            if (source.name == "stock_lossless_adapter.c" and
                    args.test_stock_fifo_bytes is not None):
                # Test-only negative-control build. The constrained value may
                # prove fail-closed cleanup, but may never enter an integrity-
                # gated, exact-trial, or diagnostic package composition.
                source_flags.append(
                    f"-DSTOCK_FIFO_BYTES={args.test_stock_fifo_bytes}u"
                )
                if args.test_stock_fifo_bytes < 27_648:
                    source_flags.append(
                        "-DSTOCK_LOSSLESS_TEST_ALLOW_UNDERSIZED_FIFO=1"
                    )
            run([*compile_flags, *source_flags,
                 "-c", source, "-o", object_path])
            all_stack_units[source.name] = parse_report(object_path.with_suffix(".su").read_text())
            if source_flags:
                source_usage = parse_stack_usage(
                    object_path.with_suffix(".su")
                )
                duplicate_stack = payload_stack_usage.keys() & source_usage.keys()
                if duplicate_stack:
                    raise SystemExit(
                        "duplicate stack-usage symbol(s): " +
                        ", ".join(sorted(duplicate_stack))
                    )
                payload_stack_usage.update(source_usage)
            objects.append(object_path)
        if args.non_audio_diagnostic or args.trial_exact_fixture:
            # The bare input name makes GNU binary symbols independent of the
            # workspace and temporary directory.  Fixture bytes are read-only.
            (temp / "diagnostic_fixture").write_bytes(fixture_bytes)
            fixture_object = temp / "diagnostic_fixture.o"
            subprocess.run([
                str(toolchain / "sh4-buildroot-linux-uclibc-objcopy"),
                "-I", "binary", "-O", "elf32-sh-linux", "-B", "sh",
                "--rename-section", ".data=.rodata.xdj700_non_audio_fixture,alloc,load,readonly,data,contents",
                "diagnostic_fixture", str(fixture_object),
            ], cwd=temp, check=True)
            objects.append(fixture_object)
        veneer = temp / "stock_lossless_veneer.o"
        veneer_defines: list[str] = []
        if args.require_stock_integrity_gate:
            veneer_defines = [
                "-DSTOCK_LOSSLESS_REQUIRE_INTEGRITY_GATE=1",
                "-DSTOCK_LOSSLESS_GATE_RETURN_PR="
                f"0x{GATE_DISPATCH_RETURN_PR[args.enable_stock_alac]:08X}",
            ]
        run([
            toolchain / "sh4-linux-gcc", "-O2", "-m4", "-ml",
            "-fno-pie", "-fno-PIC", "-fno-builtin",
            "-ffunction-sections", "-fdata-sections",
            *veneer_defines,
            "-c", ROOT / "thirdparty/build/stock_lossless_veneer.S",
            "-o", veneer,
        ])
        objects.append(veneer)
        glue = temp / "flac_glue_caller_arena.o"
        flac_source = ROOT / "thirdparty/libflac-src/src/libFLAC"
        run([
            *compile_flags,
            "-DNDEBUG", "-DHAVE_LROUND", "-DFLAC__HAS_OGG=0",
            "-DHAVE_STDINT_H", "-DHAVE_INTTYPES_H", "-DSIZEOF_VOIDP=4",
            "-DXDJ700_FLAC_BLOCKSIZE_GUARD=1",
            '-DPACKAGE_VERSION="1.4.3"',
            "-DFLAC_MOD_REQUIRE_CALLER_ARENA=1",
            *(["-DXDJ700_TARGET_RUNTIME_PROVIDER=1"]
              if args.target_runtime else []),
            "-I", flac_source, "-I", flac_source / "include",
            "-I", ROOT / "thirdparty/libflac-src/include",
            "-c", ROOT / "thirdparty/build/flac_glue.c", "-o", glue,
        ])
        all_stack_units["flac_glue.c"] = parse_report(glue.with_suffix(".su").read_text())
        linker = temp / "dual_format_payload.ld"
        write_linker_script(
            linker, args.code_vma, args.bss_vma,
            args.code_section_subalignment,
            diagnostic=bool(args.non_audio_diagnostic),
        )
        run([
            toolchain / "sh4-linux-gcc", "-Os", "-fno-pie", "-fno-PIC", "-nostdlib", "-static",
            "-Wl,--gc-sections,--build-id=none", f"-Wl,-Map,{map_path}",
            f"-Wl,-T,{linker}", "-Wl,-e,dual_format_payload_decode", "-o", elf,
            *objects, glue, link_archive, "-lgcc",
        ])

    dispatch_impl = (
        "stock_lossless_adapter_dispatch_gate_impl"
        if args.require_stock_integrity_gate
        else "stock_lossless_adapter_dispatch"
    )
    guarded_entries = (
        "dual_format_payload_decode",
        "dual_format_payload_bind_shared_storage",
        "dual_format_payload_session_open",
        "dual_format_payload_session_next",
        "dual_format_payload_session_close",
        "lossless_wave_bridge_open",
        "lossless_wave_bridge_size",
        "lossless_wave_bridge_read_at",
        "lossless_wave_bridge_close",
        "retained_format_router_classify",
        dispatch_impl,
        "stock_lossless_adapter_read",
        "stock_lossless_adapter_size",
        "stock_lossless_adapter_position",
        "stock_lossless_adapter_seek",
        "stock_lossless_adapter_eof",
        "stock_lossless_adapter_transfer",
        "stock_lossless_adapter_close",
    )
    if args.target_runtime:
        guarded_entries += ("dual_format_runtime_current",)
    if args.non_audio_diagnostic:
        guarded_entries += DIAGNOSTIC_HANDLERS
    if args.research_alac_native_fault_record:
        guarded_entries += (
            "alac_nfr_begin", "alac_nfr_bind_opening", "alac_nfr_discard",
            "alac_nfr_native_result", "alac_nfr_recognize", "alac_nfr_take_first",
        )
    missing_stack = [
        name for name in guarded_entries if name not in payload_stack_usage
    ]
    if missing_stack:
        raise SystemExit(
            "stack-usage report is missing guarded entry/entries: " +
            ", ".join(missing_stack)
        )
    oversized_stack = {
        name: byte_count for name, byte_count in payload_stack_usage.items()
        if byte_count > SH4_CODEC_STACK_FRAME_LIMIT
    }
    if oversized_stack:
        details = ", ".join(
            f"{name}={byte_count}" for name, byte_count in oversized_stack.items()
        )
        raise SystemExit(
            f"SH-4 codec stack frame exceeds {SH4_CODEC_STACK_FRAME_LIMIT} "
            f"bytes: {details}"
        )

    nm = toolchain / "sh4-buildroot-linux-uclibc-nm"
    objcopy = toolchain / "sh4-buildroot-linux-uclibc-objcopy"
    readelf = toolchain / "sh4-buildroot-linux-uclibc-readelf"
    nm_output = run([nm, "-S", elf], capture=True).stdout
    if args.flac_code_only and re.search(r"\balac_[A-Za-z0-9_]+\b", nm_output):
        raise SystemExit("FLAC-only target link retained an ALAC symbol")
    linked_stack = audit_linked(nm_output, all_stack_units, SH4_CODEC_STACK_FRAME_LIMIT)
    linked_stack["archive_sha256"] = link_archive_sha256
    linked_stack["elf_sha256"] = hashlib.sha256(elf.read_bytes()).hexdigest()
    if args.research_alac_native_fault_record:
        linked_stack["research_native_fault"] = {
            "target_stack_qualified": False,
            "unresolved": [
                "indirect source/decoder callbacks and complete call-chain depth",
                "native service and caller frames",
                "RTOS context and interrupt stack reserve",
            ],
        }
    stack_path = elf.with_suffix(".stack.json")
    stack_path.write_text(json.dumps(linked_stack, indent=2, sort_keys=True) + "\n")
    entry = symbol_value(nm_output, "dual_format_payload_decode")
    payload_end = symbol_value(nm_output, "__payload_end")
    bss_start = symbol_value(nm_output, "__bss_start")
    bss_end = symbol_value(nm_output, "__bss_end")
    if None in (entry, payload_end, bss_start, bss_end):
        raise SystemExit("linked payload is missing required layout symbols")
    assert entry is not None and payload_end is not None and bss_start is not None and bss_end is not None
    if entry != args.code_vma:
        raise SystemExit(f"entry is 0x{entry:08X}, wanted 0x{args.code_vma:08X}")
    bss_bytes = bss_end - bss_start
    if symbol_value(nm_output, "flac_default_arena") is not None:
        raise SystemExit("caller-arena payload unexpectedly links flac_default_arena")
    if symbol_value(nm_output, "flac_mod_init_arena") is None:
        raise SystemExit("caller-arena payload is missing flac_mod_init_arena")
    required_symbols = (
        "dual_format_payload_bind_shared_storage",
        "dual_format_payload_session_open",
        "dual_format_payload_session_next",
        "dual_format_payload_session_close",
        "lossless_wave_bridge_open",
        "lossless_wave_bridge_size",
        "lossless_wave_bridge_read_at",
        "lossless_wave_bridge_close",
        "retained_format_router_classify",
        "stock_lossless_dispatch_trampoline",
        "stock_lossless_read_entry",
        "stock_lossless_read_original",
        "stock_lossless_adapter_dispatch",
        "stock_lossless_adapter_read",
        "stock_lossless_adapter_size",
        "stock_lossless_adapter_position",
        "stock_lossless_adapter_seek",
        "stock_lossless_adapter_eof",
        "stock_lossless_adapter_transfer",
        "stock_lossless_adapter_close",
        "stock_lossless_integrity_gate_required",
    )
    if args.require_stock_integrity_gate:
        required_symbols += (
            "stock_lossless_adapter_dispatch_gate_impl",
            "stock_lossless_gate_hook_return_pr",
        )
    if args.target_runtime:
        required_symbols += ("dual_format_runtime_current",)
    if args.non_audio_diagnostic:
        required_symbols += DIAGNOSTIC_HANDLERS + tuple(diagnostic_policy)
    for symbol in required_symbols:
        value = symbol_value(nm_output, symbol)
        if value is None or value < args.code_vma or value >= payload_end:
            raise SystemExit(
                f"caller-arena payload is missing in-range export {symbol}"
            )
    if args.require_stock_integrity_gate:
        symbol_table = run([readelf, "-W", "-s", elf], capture=True).stdout
        public_dispatch = readelf_symbol(
            symbol_table, "stock_lossless_adapter_dispatch"
        )
        gate_impl = readelf_symbol(
            symbol_table, "stock_lossless_adapter_dispatch_gate_impl"
        )
        if public_dispatch is None or public_dispatch.get("type") != "FUNC" or \
                public_dispatch.get("size") != 4:
            raise SystemExit(
                "production public adapter dispatch is not the inert return stub"
            )
        if gate_impl is None or gate_impl.get("type") != "FUNC" or \
                gate_impl.get("bind") != "GLOBAL" or \
                gate_impl.get("visibility") != "HIDDEN":
            raise SystemExit(
                "production gate-only dispatch implementation is not hidden"
            )
    if bss_bytes >= FLAC_CALLER_ARENA_BYTES:
        raise SystemExit(
            f"caller-arena payload BSS is {bss_bytes} bytes; fixed 128 KiB "
            "FLAC arena appears to remain linked"
        )
    if args.target_runtime and bss_bytes != 0:
        raise SystemExit(
            f"target-runtime payload retained {bss_bytes} bytes of linked BSS"
        )
    validate_layout(
        elf, readelf, code_vma=args.code_vma, payload_end=payload_end,
        xip_vma_limit=args.xip_vma_limit, bss_vma=args.bss_vma,
        bss_start=bss_start, bss_end=bss_end, max_bss_bytes=args.max_bss_bytes,
        require_bss=not args.target_runtime,
    )
    run([objcopy, "-O", "binary", "--gap-fill=0",
         *(f"--only-section={section}" for section in FLAT_OUTPUT_SECTIONS), elf, output])
    payload_size = output.stat().st_size
    if payload_size != payload_end - args.code_vma:
        raise SystemExit("objcopy did not emit the complete flat payload range")
    if args.non_audio_diagnostic:
        payload_bytes = output.read_bytes()
        symbols = run([readelf, "-W", "-s", elf], capture=True).stdout
        sections = parse_sections(run([readelf, "-W", "-S", elf], capture=True).stdout)
        rodata = next(section for section in sections if section["name"] == ".rodata")
        ro_start = int(rodata["address"])
        ro_end = ro_start + int(rodata["size"])
        if "W" in str(rodata["flags"]) or "X" in str(rodata["flags"]):
            raise SystemExit("diagnostic fixture/policy must be nonexecutable read-only data")
        for name, expected in diagnostic_policy.items():
            symbol = readelf_symbol(symbols, name)
            if symbol is None or symbol["type"] != "OBJECT" or symbol["size"] != 4:
                raise SystemExit(f"invalid diagnostic policy symbol: {name}")
            address = int(symbol["value"])
            offset = address - args.code_vma
            if not ro_start <= address <= ro_end - 4 or int.from_bytes(
                    payload_bytes[offset:offset + 4], "little") != expected:
                raise SystemExit(f"diagnostic policy mismatch: {name}")
        start = symbol_value(nm_output, "_binary_diagnostic_fixture_start")
        end = symbol_value(nm_output, "_binary_diagnostic_fixture_end")
        size_symbol = readelf_symbol(symbols, "_binary_diagnostic_fixture_size")
        if start is None or end is None or not ro_start <= start < end <= ro_end or \
                end - start != len(fixture_bytes) or size_symbol is None or \
                size_symbol["section"] != "ABS" or size_symbol["value"] != len(fixture_bytes) or \
                payload_bytes[start - args.code_vma:end - args.code_vma] != fixture_bytes:
            raise SystemExit("diagnostic fixture symbol extent or payload bytes differ")
        print(f"Non-audio diagnostic: {args.non_audio_diagnostic}; exact fixture and caller policies verified")
    policy_address = symbol_value(
        nm_output, "stock_lossless_integrity_gate_required"
    )
    assert policy_address is not None
    policy_offset = policy_address - args.code_vma
    policy_value = int.from_bytes(
        output.read_bytes()[policy_offset:policy_offset + 4], "little"
    )
    expected_policy = 1 if args.require_stock_integrity_gate else 0
    if policy_value != expected_policy:
        raise SystemExit(
            "linked stock integrity-gate policy does not match build mode"
        )
    if args.require_stock_integrity_gate:
        payload_bytes = output.read_bytes()
        impl_address = symbol_value(
            nm_output, "stock_lossless_adapter_dispatch_gate_impl"
        )
        assert impl_address is not None
        return_pr = GATE_DISPATCH_RETURN_PR[args.enable_stock_alac]
        hook_return_pr = GATE_HOOK_RETURN_PR[args.enable_stock_alac]
        hook_policy_address = symbol_value(
            nm_output, "stock_lossless_gate_hook_return_pr"
        )
        assert hook_policy_address is not None
        hook_policy_offset = hook_policy_address - args.code_vma
        if int.from_bytes(
                payload_bytes[hook_policy_offset:hook_policy_offset + 4],
                "little") != hook_return_pr:
            raise SystemExit(
                "production payload close provenance policy does not match mode"
            )
        if payload_bytes.count(impl_address.to_bytes(4, "little")) != 1:
            raise SystemExit(
                "gate-only dispatch implementation does not have exactly one "
                "payload reference"
            )
        if payload_bytes.count(return_pr.to_bytes(4, "little")) != 1:
            raise SystemExit(
                "production payload does not contain exactly one expected "
                "gate-return provenance literal"
            )
        if payload_bytes.count(hook_return_pr.to_bytes(4, "little")) < 1:
            raise SystemExit(
                "production payload does not contain its expected gate-hook "
                "return provenance literal"
            )
    if args.flac_code_only:
        sections = parse_sections(run([readelf, "-W", "-S", elf], capture=True).stdout)
        if any(section["name"].startswith(".got") and section["size"]
               for section in sections):
            raise SystemExit("FLAC-only payload unexpectedly needs a SH GOT")
    else:
        got_base, clz_slot, clz_table = validate_static_sh_got(
            output, elf, readelf, nm_output,
            code_vma=args.code_vma, payload_end=payload_end,
        )
    print(f"payload: entry=0x{entry:08X} bytes={payload_size} "
          f"bss=0x{bss_start:08X}-0x{bss_end:08X} ({bss_bytes} bytes)")
    if args.flac_code_only:
        print("SH GOT: absent in ALAC-code-free link")
    else:
        print(
            f"SH GOT: base=0x{got_base:08X} __clz_tab-slot=0x{clz_slot:08X} "
            f"target=0x{clz_table:08X}"
        )
    print(f"FLAC arena: caller-owned {FLAC_CALLER_ARENA_BYTES} bytes; "
          "fixed linked arena absent")
    if args.target_runtime:
        print("Runtime state: stock-adapter allocation; linked BSS absent")
    print(
        "Stock adapter integrity gate: "
        + ("required" if args.require_stock_integrity_gate else "direct probe allowed")
    )
    print(
        "SH-4 retained stack: "
        f"one-shot={payload_stack_usage['dual_format_payload_decode']} bytes, "
        f"largest={max(payload_stack_usage.values())} bytes, "
        f"guard={SH4_CODEC_STACK_FRAME_LIMIT} bytes "
        "(payload/bridge/router static frames only)"
    )
    print(f"SH-4 linked C stack: functions={len(linked_stack['linked_c_frames'])} "
          f"largest={linked_stack['largest']} bytes read_frame_="
          f"{linked_stack['linked_c_frames']['read_frame_']['bytes']} bytes; "
          f"assembly/runtime review symbols={len(linked_stack['assembly_or_runtime_symbols'])}")
    if args.research_alac_native_fault_record:
        print("ALAC native-fault research: target stack UNQUALIFIED; indirect "
              "callbacks, native/caller frames and RTOS/interrupt reserve unresolved")
    print(f"wrote {output}")
    print(f"wrote {elf}")
    print(f"wrote {map_path}")
    print("Dormant codec/bridge/adapter payload: stock redirects are exported "
          "but not installed by this builder. The BSS VMA is a link-time test "
          "layout, not a claimed safe device RAM address; no firmware image or "
          "hardware flashability is asserted.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        if error.stdout:
            sys.stderr.write(error.stdout)
        if error.stderr:
            sys.stderr.write(error.stderr)
        raise
