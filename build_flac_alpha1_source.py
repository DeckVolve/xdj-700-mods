#!/usr/bin/env python3
"""Rebuild the fixed FLAC alpha.1 sparse template from source and owner firmware.

This produces a template and private intermediate binaries, never an update or
USB image. The released patcher separately consumes the verified template.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import zlib

sys.dont_write_bytecode = True

import patcher

ROOT = Path(__file__).resolve().parent
BASE = 0x08000000
PAYLOAD_VMA = 0x091BD7B0
HEADER_VMA = 0x091BD7A0
TARGET_SIZE = 18_655_132
TARGET_SHA = "5d8a80a1a991d6209d181d4098dc1cca5c7da3b1b2d79394242dbc5c6ccefa53"
TEMPLATE_SHA = "b54d831e8881eaab587115c9deadfccc69fbb41cdfdea39ae547f8ac25356462"
PAYLOAD_SHA = "522e99437d48831800dc19b6fc29da7fa8c864eb4484f0fe08efe415c0a79cb3"
GATE_SHA = "32f301307e8e777617e258e973d639716452823719cf738558103465c7d7697e"
SATELLITE_SHA = "23c373656b98c74ec78e27816dc817c94b614520870d967a7c3097bab9048f51"
CACHE_SHA = "dc039050d43c4b4c87a51eb0e9da4a1169594aa4b7a88638f761a48ffc9f4b74"
TOOL_SHA = {
    "sh4-linux-gcc": "7fb20ec7ed6b16e9243ff8c6f9f100a6ad3924cdbc187968adbcdd215208a23f",
    "sh4-buildroot-linux-uclibc-ar": "3bb9106d074411dce75513845d734a3632e640212e3c0baf33f01457b0807f7b",
    "sh4-buildroot-linux-uclibc-objcopy": "d5651b2a5d28283d202f402a2c972662b92215a5d6ef519b603272285a8c627b",
}
LIBGCC_SHA = "b5717d3562f23ad535ee0740b8bfb602c34ab70efdd40eaea0eb956d73f20fad"
TARGET_SYMBOLS = {
    "dispatch": "stock_lossless_dispatch_trampoline",
    "read": "stock_lossless_read_entry",
    "size": "stock_lossless_adapter_size",
    "position": "stock_lossless_adapter_position",
    "seek": "stock_lossless_adapter_seek",
    "eof": "stock_lossless_adapter_eof",
    "transfer": "stock_lossless_adapter_transfer",
    "close": "stock_lossless_adapter_close",
}
# Address and length metadata only. The instruction values come exclusively
# from the exact owner-supplied official v1.15 update at build time.
COPY = (
    ("payload-dispatch", 0x091BE720, 0x08C01E78, 12, None),
    ("payload-control", 0x091BE74A, 0x08C01E84, 20, None),
    ("payload-read", 0x091BE790, 0x08C5635E, 12, None),
    ("gate-dispatch", 0x080FB8FC, 0x08C01E78, 32, (28, "bf_s", 0x080FB922)),
    ("gate-read", 0x080FB974, 0x08C5635E, 12, None),
    ("cache-a", 0x080FC100, 0x08BFFE72, 4, (2, "bf", 0x080FC118)),
    ("cache-b", 0x080FC11E, 0x08BFFE76, 4, None),
    ("cache-c", 0x080FC122, 0x08BFFE7C, 2, None),
)
HOOKS = (
    ("dispatch", 0x08C01E78, 32), ("read", 0x08C5635E, 12),
    ("size", 0x08C567BC, 4), ("position", 0x08BE9000, 4),
    ("eof", 0x08BE900C, 4), ("seek", 0x08BE9010, 4),
    ("transfer", 0x08BE9018, 4), ("close", 0x08C000A4, 4),
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked(data: bytes, length: int, digest: str, label: str) -> bytes:
    require((len(data), sha(data)) == (length, digest), f"{label} identity differs")
    return data


def run(*argv: object, env: dict[str, str] | None = None) -> str:
    stable_env = os.environ.copy() if env is None else env.copy()
    stable_env["LC_ALL"] = "C"
    try:
        result = subprocess.run([str(item) for item in argv], cwd=ROOT,
                                env=stable_env, check=True, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or "").strip()[-3000:]
        raise ValueError(f"build command failed ({Path(str(argv[0])).name}): {detail}") from error
    return result.stdout


def source_manifest() -> None:
    manifest_path = ROOT / "SOURCE_BUILD_MANIFEST.json"
    require(manifest_path.is_file() and not manifest_path.is_symlink(), "source manifest missing")
    manifest = json.loads(manifest_path.read_bytes())
    require(manifest.get("schema") == 1, "source manifest schema differs")
    paths = set()
    for entry in manifest["files"]:
        rel = entry["path"]
        require(isinstance(rel, str) and rel == Path(rel).as_posix() and
                not Path(rel).is_absolute() and ".." not in Path(rel).parts and
                rel not in paths, "source manifest path differs")
        paths.add(rel)
        path = ROOT / rel
        require(path.is_file() and not path.is_symlink(), f"source missing: {rel}")
        checked(path.read_bytes(), entry["bytes"], entry["sha256"], rel)
    # An unlisted header/source in an include directory could change a build.
    for base in (ROOT / "thirdparty", ROOT / "tools"):
        for path in base.rglob("*"):
            require(not path.is_symlink(), f"source symlink: {path}")
            if path.is_file():
                require(path.relative_to(ROOT).as_posix() in paths,
                        f"unlisted build-tree file: {path}")


def symbols(tool: Path, elf: Path) -> dict[str, int]:
    result = {}
    for line in run(tool / "sh4-buildroot-linux-uclibc-nm", "-n", elf).splitlines():
        fields = line.split()
        if len(fields) >= 3:
            try:
                result[fields[-1]] = int(fields[0], 16)
            except ValueError:
                pass
    return result


def owner_copy(stock: bytes, name: str, dest: int, source: int, count: int,
               branch: tuple[int, str, int] | None) -> tuple[bytes, dict]:
    value = bytearray(stock[source - BASE:source - BASE + count])
    require(len(value) == count, f"{name} owner source is short")
    record = {"name": name, "dest_offset": dest - BASE,
              "source_offset": source - BASE, "size": count}
    if branch:
        offset, kind, target = branch
        require(value[offset + 1] == {"bf": 0x8B, "bf_s": 0x8F}[kind],
                f"{name} owner branch opcode differs")
        delta = target - (dest + offset + 4)
        require(delta % 2 == 0 and -128 <= delta // 2 <= 127,
                f"{name} branch displacement differs")
        value[offset] = (delta // 2) & 0xFF
        record["branch_relocation"] = {"instruction_offset": offset,
                                       "kind": kind, "target_vma": target}
    return bytes(value), record


def fill_holes(binary: bytes, base: int, stock: bytes, names: set[str]) -> bytes:
    result = bytearray(binary)
    for name, dest, source, count, branch in COPY:
        if name not in names:
            continue
        start = dest - base
        require(0 <= start <= len(result) - count and
                result[start:start + count] == bytes(count),
                f"{name} source placeholder differs")
        value, _ = owner_copy(stock, name, dest, source, count, branch)
        result[start:start + count] = value
    return bytes(result)


def far_jump(address: int, target: int, size: int) -> bytes:
    require(address % 2 == target % 2 == size % 2 == 0 and size >= 10,
            "unaligned far jump")
    literal = (address + 9) & ~3
    base = (address + 4) & ~3
    require((literal - base) % 4 == 0 and 0 <= (literal - base) // 4 <= 255,
            "far jump displacement differs")
    value = bytearray(((literal - base) // 4, 0xD0, 0x2B, 0x40, 0x09, 0x00))
    while address + len(value) < literal:
        value += b"\x09\x00"
    value += target.to_bytes(4, "little")
    while len(value) < size:
        value += b"\x09\x00"
    require(len(value) == size, "far jump size differs")
    return bytes(value)


def build_gate(out: Path, tool: Path, stock: bytes, blob: bytes,
               payload_symbols: dict[str, int]) -> tuple[bytes, bytes, bytes, dict[str, int]]:
    defines = [f"-DXDJ700_GATE_PAYLOAD_BYTES={len(blob)}",
               f"-DXDJ700_GATE_PAYLOAD_CRC32=0x{zlib.crc32(blob) & 0xffffffff:08X}",
               "-DXDJ700_GATE_PAYLOAD_READ_VMA=0xA91BD7A0",
               "-DXDJ700_GATE_ENABLE_ALAC=0", "-DXDJ700_GATE_ENABLE_CACHE_REPAIR=1",
               "-DXDJ700_GATE_REQUIRE_DIAGNOSTIC_ISLAND_ZERO=0",
               "-DXDJ700_GATE_USE_EXTERNAL_READER_RELEASE=0"]
    for name, symbol in TARGET_SYMBOLS.items():
        require(symbol in payload_symbols, f"missing payload symbol: {symbol}")
        defines.append(f"-DXDJ700_GATE_{name.upper()}_TARGET=0x{payload_symbols[symbol]:08X}")
    obj = out / "gate.o"
    elf = out / "gate.elf"
    run(tool / "sh4-linux-gcc", "-m4a", "-ml", "-x", "assembler-with-cpp",
        *defines, "-c", ROOT / "thirdparty/build/xdj700_lossless_tail_gate.S", "-o", obj)
    run(tool / "sh4-linux-gcc", "-m4a", "-ml", "-nostdlib", "-static",
        "-Wl,--build-id=none", f"-Wl,-T,{ROOT / 'thirdparty/build/xdj700_lossless_tail_gate.ld'}",
        "-o", elf, obj)
    require("There are no relocations in this file" in
            run(tool / "sh4-buildroot-linux-uclibc-readelf", "-W", "-r", elf),
            "gate has unresolved relocations")
    command = [tool / "sh4-buildroot-linux-uclibc-objcopy", "-O", "binary",
               "--gap-fill=0xFF"]
    run(*command, "--only-section=.text", "--only-section=.xdj700_lossless_gate_state",
        elf, out / "gate.raw")
    run(*command, "--only-section=.xdj700_lossless_gate_satellite",
        elf, out / "satellite.raw")
    run(*command, "--only-section=.xdj700_lossless_cache_gate",
        elf, out / "cache.raw")
    gate = (out / "gate.raw").read_bytes().ljust(768, b"\xff")
    sat = (out / "satellite.raw").read_bytes().ljust(128, b"\xff")
    cache = (out / "cache.raw").read_bytes().ljust(128, b"\xff")
    require((len(gate), len(sat), len(cache)) == (768, 128, 128),
            "gate region grew")
    gate = fill_holes(gate, 0x080FB800, stock, {"gate-dispatch", "gate-read"})
    cache = fill_holes(cache, 0x080FC100, stock, {"cache-a", "cache-b", "cache-c"})
    checked(gate, 768, GATE_SHA, "compiled gate")
    checked(sat, 128, SATELLITE_SHA, "compiled satellite")
    checked(cache, 128, CACHE_SHA, "compiled cache gate")
    return gate, sat, cache, symbols(tool, elf)


def assemble_decoded(stock: bytes, payload: bytes, gate: bytes, satellite: bytes,
                     cache: bytes, gate_symbols: dict[str, int]) -> bytes:
    result = bytearray(stock + b"\xff" * (TARGET_SIZE - len(stock)))
    require(len(result) == TARGET_SIZE and result[0x740:0x745] == b"1.15\0",
            "stock decoded layout differs")
    def place(address: int, value: bytes, *, erased: bool = False) -> None:
        start = address - BASE
        require(0 <= start <= len(result) - len(value), "patch range differs")
        if erased:
            require(result[start:start + len(value)] == b"\xff" * len(value),
                    f"patch cave at {address:#x} is not erased")
        result[start:start + len(value)] = value
    for name, address, length in HOOKS:
        key = "xdj700_lossless_gate_" + name
        require(key in gate_symbols, f"missing gate symbol: {key}")
        target = gate_symbols[key]
        if name == "dispatch":
            value = far_jump(address, target, 12) + bytes(20)
        elif name == "read":
            value = far_jump(address, target, length)
        else:
            value = target.to_bytes(4, "little")
        require(len(value) == length, "hook length differs")
        place(address, value)
    place(0x080FB800, gate, erased=True)
    require(result[0xFBB00:0xFBB80] == b"\xff" * 128,
            "gate/satellite gap differs")
    place(0x080FBB80, satellite, erased=True)
    place(0x080FC100, cache, erased=True)
    place(0x08BFFE72, far_jump(0x08BFFE72, 0x080FC100, 12))
    result[0x743] = ord("4")
    header = struct.pack("<4sBBHII", b"X7LS", 16, 1, 1,
                         len(payload), zlib.crc32(payload) & 0xffffffff)
    require(len(header) == 16, "payload header length differs")
    place(HEADER_VMA, header + payload, erased=True)
    return checked(bytes(result), TARGET_SIZE, TARGET_SHA, "rebuilt decoded image")


def make_recipe(stock: bytes, target: bytes) -> bytes:
    template = bytearray(target)
    operations = []
    for name, dest, source, count, branch in COPY:
        value, record = owner_copy(stock, name, dest, source, count, branch)
        start = dest - BASE
        require(template[start:start + count] == value,
                f"{name} compiled relocation differs from owner input")
        template[start:start + count] = bytes(count)
        operations.append(record)
    before = stock + b"\xff" * (len(target) - len(stock))
    spans = []
    i = 0
    while i < len(template):
        if template[i] == before[i]:
            i += 1
            continue
        start = i
        while i < len(template) and template[i] != before[i]:
            i += 1
        spans.append({"offset": start,
                      "data": base64.b64encode(template[start:i]).decode("ascii")})
    recipe = {"schema": 1,
              "artifact_kind": "sparse-decoded-template-with-owner-stock-relocation-holes",
              "stock_decoded_size": len(stock), "stock_decoded_sha256": sha(stock),
              "target_decoded_size": len(target), "target_decoded_sha256": sha(target),
              "spans": spans, "owner_stock_copy_ops": operations}
    encoded = (json.dumps(recipe, sort_keys=True, separators=(",", ":")) + "\n").encode()
    checked(encoded, 80_481, TEMPLATE_SHA, "rebuilt sparse template")
    require(patcher.apply_recipe(stock, recipe) == target,
            "rebuilt sparse template does not reconstruct target")
    return encoded


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--official-upd", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True,
                        help="bin directory of the exact pinned Bootlin SH-4 toolchain")
    parser.add_argument("--output-dir", type=Path, required=True,
                        help="new private local directory ending .DO_NOT_FLASH")
    args = parser.parse_args()
    source_manifest()
    official = args.official_upd
    require(official.is_file() and not official.is_symlink() and
            stat.S_ISREG(official.lstat().st_mode), "official input is not a regular file")
    require(official.stat().st_size == patcher.STOCK_UPD[0], "official input size differs")
    official_bytes = checked(official.read_bytes(), *patcher.STOCK_UPD, "official update")
    main, _ = patcher.stock_images(official_bytes)
    stock = patcher.decode_section(main, 0x40000)
    require((len(stock), sha(stock)) ==
            (18_601_864, "1875381b56d065a2b0a97a63b64ead5ce71397c521b7a62713c5bb4a0e055939"),
            "official decoded image differs")
    tool = args.toolchain.resolve(strict=True)
    for name, expected in TOOL_SHA.items():
        path = tool / name
        require(path.is_file() and sha(path.read_bytes()) == expected,
                f"toolchain member differs: {name}")
    libgcc = (tool / "../lib/gcc/sh4-buildroot-linux-uclibc/14.3.0/libgcc.a").resolve()
    require(libgcc.is_file() and sha(libgcc.read_bytes()) == LIBGCC_SHA,
            "toolchain libgcc differs")
    out = args.output_dir
    require(out.is_absolute() and out.name.endswith(".DO_NOT_FLASH") and
            not out.exists() and not out.is_symlink(), "output must be a new absolute .DO_NOT_FLASH directory")
    parent = out.parent.resolve(strict=True)
    require(parent == out.parent and ROOT != parent and ROOT not in parent.parents and
            official.resolve(strict=True) != out, "output must be outside source tree")
    out.mkdir(mode=0o700)
    env = os.environ.copy()
    env.update(XDJ700_TOOLCHAIN=str(tool), XDJ700_FLAC_BUILD_DIR=str(out / "libflac"),
               XDJ700_LIBFLAC_OPTIMIZATION="-Os", PYTHONDONTWRITEBYTECODE="1")
    print("Building pinned FLAC source archive...", file=sys.stderr, flush=True)
    run("bash", ROOT / "tools/build_libflac_sh4.sh", env=env)
    print("Building FLAC-only SH-4 payload...", file=sys.stderr, flush=True)
    run(sys.executable, "-B", ROOT / "tools/build_dual_format_payload.py",
        "--target-runtime", "--require-stock-integrity-gate", "--disable-stock-alac",
        "--flac-code-only", "--code-vma", "0x091BD7B0", "--code-vma-limit", "0x09200000",
        "--code-section-subalignment", "4", "--toolchain", tool,
        "--build-dir", out / "libflac", "--skip-build", "--output", out / "payload.zero.bin",
        "--elf", out / "payload.zero.elf", "--map", out / "payload.map", env=env)
    binary = (out / "payload.zero.bin").read_bytes()
    require(len(binary) == 53_228, "compiled FLAC payload length differs")
    payload = fill_holes(binary, PAYLOAD_VMA, stock,
                         {"payload-dispatch", "payload-control", "payload-read"})
    checked(payload, 53_228, PAYLOAD_SHA, "compiled owner-filled FLAC payload")
    payload_symbols = symbols(tool, out / "payload.zero.elf")
    header = struct.pack("<4sBBHII", b"X7LS", 16, 1, 1, len(payload),
                         zlib.crc32(payload) & 0xffffffff)
    print("Building target gates and sparse template...", file=sys.stderr, flush=True)
    gate, sat, cache, gate_symbols = build_gate(out, tool, stock, header + payload,
                                                payload_symbols)
    target = assemble_decoded(stock, payload, gate, sat, cache, gate_symbols)
    recipe = make_recipe(stock, target)
    require(official.read_bytes() == official_bytes, "official input changed during build")
    (out / "flac_alpha1_template.json").write_bytes(recipe)
    report = {"schema": 1, "scope": "source-only FLAC alpha.1 template rebuild",
              "firmware_installable": False, "official_upd_sha256": sha(official_bytes),
              "payload_sha256": sha(payload), "gate_sha256": sha(gate),
              "satellite_sha256": sha(sat), "cache_sha256": sha(cache),
              "target_decoded_sha256": sha(target), "template_bytes": len(recipe),
              "template_sha256": sha(recipe)}
    (out / "source-build-result.json").write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"source build refused: {error}", file=sys.stderr)
        raise SystemExit(2)
