#!/usr/bin/env python3
"""Rebuild the fixed XDJ-700 FLAC alpha.1 update from an owner-supplied official UPD."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile

sys.dont_write_bytecode = True

from lzss_pioneer import append_section_checksum, decode_section, encode_section, validate_section_checksum
from srecord_update import build_upd, validate_crc16_le

STOCK_UPD = (17_371_335, '73edec9802da51672257c2599efc04209dc92478fcbaa1a0425b3b122e33f99c')
STOCK_MAIN = (0x006EA7C0, 'de683f253eba02e86ada5c89f2302a0f3a45331ffdcb5e6f9f20359aa6f6ce3a')
STOCK_PANL = (0x40000, '52c5a54320c11477c50ed1da93fc585128c50e9e78d624c5ae27a8f4ad4c3a99')
TARGET_UPD = (17_479_419, '8814d02f13e8d7a9feaa8bb6f45a11fccd174d144737cc009b087ae7b5c089fb')
TARGET_MAIN = (7_296_244, '79aef473a2223e3057a76da3f2c87b62e649d2b48e75a0984db73b1dfd46dc43')
TARGET_DECODED = (18_655_132, '5d8a80a1a991d6209d181d4098dc1cca5c7da3b1b2d79394242dbc5c6ccefa53')
RECIPE = Path(__file__).resolve().parent / 'flac_alpha1_template.json'
RECIPE_BYTES = 80_481
RECIPE_SHA = 'b54d831e8881eaab587115c9deadfccc69fbb41cdfdea39ae547f8ac25356462'

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def require(value: bool, message: str) -> None:
    if not value:
        raise ValueError(message)

def pinned(value: bytes, identity: tuple[int, str], name: str) -> bytes:
    require((len(value), sha(value)) == identity, f'{name} identity differs')
    return value

def split_upd(update: bytes) -> tuple[bytes, bytes]:
    first = update.find(b'\r\n')
    second = update.find(b'\r\n', first + 2)
    require(0 < first < second, 'combined document lengths missing')
    a, b = update[:first], update[first + 2:second]
    require(re.fullmatch(rb'[1-9][0-9]*', a) is not None and
            re.fullmatch(rb'[1-9][0-9]*', b) is not None,
            'combined document lengths malformed')
    start = second + 2
    count_a, count_b = int(a), int(b)
    require(start + count_a + count_b == len(update), 'combined document lengths differ')
    return update[start:start + count_a], update[start + count_a:]

def parse_document(document: bytes, kind: str, length: int, base: int,
                   version: bytes, count: int) -> bytes:
    require(validate_crc16_le(document), f'{kind} document CRC differs')
    header = b'XDJ-700     ' + kind.encode() + version + (b'\0' if kind == 'MAIN' else b'')
    require(document.startswith(header), f'{kind} descriptor differs')
    body = document[len(header):-2]
    require(b'\r\n' in body, f'{kind} lacks S-records')
    lines = body.split(b'\r\n')
    require(lines[-1] == b'', f'{kind} final CRLF missing')
    lines = lines[:-1]
    image = bytearray(b'\xff' * length)
    previous = base
    seen = 0
    for index, line in enumerate(lines):
        # The version descriptor can include fixed spacing before its S0 line.
        if index == 0:
            line = line.lstrip(b' 0') if kind == 'MAIN' else line.lstrip(b' ')
        require(re.fullmatch(rb'S[0287][0-9A-F]+', line) is not None,
                f'{kind} S-record syntax differs')
        raw = bytes.fromhex(line[2:].decode('ascii'))
        require(len(raw) >= 2 and len(raw) == raw[0] + 1 and sum(raw) & 0xff == 0xff,
                f'{kind} S-record checksum differs')
        typ = line[1:2]
        if typ == b'2':
            seen += 1
            addr = int.from_bytes(raw[1:4], 'big')
            value = raw[4:-1]
            require(value and previous <= addr and base <= addr < addr + len(value) <= base + length,
                    f'{kind} S-record range differs')
            image[addr - base:addr - base + len(value)] = value
            previous = addr + len(value)
        else:
            require((index == 0 and typ == b'0') or
                    (index == len(lines) - 1 and typ in (b'7', b'8')),
                    f'{kind} S-record framing differs')
    require(seen == count, f'{kind} S-record count differs')
    return bytes(image)

def stock_images(update: bytes) -> tuple[bytes, bytes]:
    pinned(update, STOCK_UPD, 'official UPD')
    main_doc, panl_doc = split_upd(update)
    main = parse_document(main_doc, 'MAIN', STOCK_MAIN[0], 0, b'Ver1.15', 222510)
    panl = parse_document(panl_doc, 'PANL', STOCK_PANL[0], 0x0C0000, b'Ver1.00', 207)
    pinned(main, STOCK_MAIN, 'official MAIN')
    pinned(panl, STOCK_PANL, 'official PANL')
    require(build_upd(main, panl, main_ver=b'Ver1.15',
                      source_upd_bytes=update) == update,
            'ordinary serializer did not reproduce official update')
    return main, panl

def apply_recipe(stock: bytes, recipe: dict) -> bytes:
    require((len(stock), sha(stock)) ==
            (recipe['stock_decoded_size'], recipe['stock_decoded_sha256']),
            'official decoded image differs')
    result = bytearray(stock + b'\xff' * (recipe['target_decoded_size'] - len(stock)))
    occupied = bytearray(len(result))
    for span in recipe['spans']:
        start = span['offset']
        data = base64.b64decode(span['data'], validate=True)
        end = start + len(data)
        require(0 <= start < end <= len(result) and not any(occupied[start:end]),
                'template span overlaps or exceeds decoded image')
        result[start:end] = data
        occupied[start:end] = b'\x01' * len(data)
    for operation in recipe['owner_stock_copy_ops']:
        start, source, count = (operation[key] for key in ('dest_offset', 'source_offset', 'size'))
        require(0 <= source < source + count <= len(stock) and
                0 <= start < start + count <= len(result), 'owner-copy span out of range')
        copied = bytearray(stock[source:source + count])
        branch = operation.get('branch_relocation')
        if branch is not None:
            offset = branch['instruction_offset']
            opcode_high = {'bf': 0x8B, 'bf_s': 0x8F}[branch['kind']]
            require(0 <= offset < count - 1 and copied[offset + 1] == opcode_high,
                    'owner-copy branch opcode differs')
            branch_pc = 0x08000000 + start + offset
            delta = branch['target_vma'] - (branch_pc + 4)
            require(delta % 2 == 0 and -128 <= delta // 2 <= 127,
                    'branch relocation exceeds SH-4 displacement range')
            copied[offset] = (delta // 2) & 0xFF
        result[start:start + count] = copied
    complete = bytes(result)
    pinned(complete, TARGET_DECODED, 'assembled target decoded image')
    return complete

def build(official: bytes, recipe_bytes: bytes) -> bytes:
    pinned(recipe_bytes, (RECIPE_BYTES, RECIPE_SHA), 'fixed recipe')
    recipe = json.loads(recipe_bytes)
    require(recipe.get('schema') == 1, 'recipe schema differs')
    main, panl = stock_images(official)
    validate_section_checksum(main, 0x40000)
    decoded = decode_section(main, 0x40000)
    modified = apply_recipe(decoded, recipe)
    print('Encoding fixed alpha.1 application; this can take a few minutes...',
          file=sys.stderr, flush=True)
    main_new = main[:0x40000] + append_section_checksum(encode_section(modified))
    pinned(main_new, TARGET_MAIN, 'rebuilt MAIN')
    validate_section_checksum(main_new, 0x40000)
    require(decode_section(main_new, 0x40000) == modified,
            'rebuilt MAIN decompression differs')
    update = build_upd(main_new, panl, main_ver=b'Ver1.16',
                       source_upd_bytes=official)
    pinned(update, TARGET_UPD, 'rebuilt alpha.1 UPD')
    return update

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--official-upd', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.official_upd
    target = args.output if args.output.is_absolute() else Path.cwd() / args.output
    source_stat = source.lstat()
    require(stat.S_ISREG(source_stat.st_mode) and not source.is_symlink(),
            'official input must be a regular nonsymlink file')
    require(source_stat.st_size == STOCK_UPD[0], 'official input size differs')
    require(target.parent.is_dir() and
            target.parent.resolve(strict=True) == target.parent and
            not target.exists() and not target.is_symlink(),
            'output must be a new file under a real directory')
    require(source.resolve(strict=True) != target.resolve(strict=False), 'input and output alias')
    original = source.read_bytes()
    pinned(original, STOCK_UPD, 'official UPD')
    require(RECIPE.is_file() and not RECIPE.is_symlink(),
            'fixed template must be a regular nonsymlink file')
    recipe = RECIPE.read_bytes()
    update = build(original, recipe)
    require(source.read_bytes() == original, 'official input changed during build')
    descriptor, temporary_name = tempfile.mkstemp(
        prefix='.xdj700-alpha1-', suffix='.tmp', dir=target.parent)
    temporary = Path(temporary_name)
    created = os.fstat(descriptor)
    try:
        with os.fdopen(descriptor, 'wb') as out:
            out.write(update)
            out.flush()
            os.fsync(out.fileno())
        pinned(temporary.read_bytes(), TARGET_UPD, 'temporary alpha.1 UPD')
        # Hard-link creation is atomic and fails if the requested output exists.
        # This keeps a partial update from ever appearing under an UPD name.
        try:
            os.link(temporary, target, follow_symlinks=False)
        except FileExistsError as error:
            raise ValueError('output appeared during build; refusing overwrite') from error
        except OSError as error:
            raise ValueError('output filesystem must support hard links') from error
        pinned(target.read_bytes(), TARGET_UPD, 'written alpha.1 UPD')
    finally:
        try:
            visible = temporary.lstat()
            if (visible.st_dev, visible.st_ino) == (created.st_dev, created.st_ino) and \
                    stat.S_ISREG(visible.st_mode):
                temporary.unlink()
        except OSError:
            pass
    print(json.dumps({'output': str(target), 'bytes': len(update), 'sha256': sha(update)}))
    return 0

if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError) as error:
        print(f'error: {error}', file=sys.stderr)
        raise SystemExit(2)
