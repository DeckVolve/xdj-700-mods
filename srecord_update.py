"""Fixed XDJ-700 update-container serializer for the owner-input patcher."""

import binascii
import struct

GAPS = ((0x0002A0, 0x000300), (0x0007E0, 0x000900),
        (0x000C40, 0x010000), (0x02F340, 0x040000))


def crc16_ccitt(data: bytes) -> int:
    return binascii.crc_hqx(data, 0) & 0xFFFF


def append_crc16_le(data: bytes) -> bytes:
    return data + struct.pack('<H', crc16_ccitt(data))


def validate_crc16_le(document: bytes) -> bool:
    return len(document) >= 2 and int.from_bytes(document[-2:], 'little') == crc16_ccitt(document[:-2])


def s2(address: int, data: bytes) -> bytes:
    count = len(data) + 4
    body = bytes((count,)) + address.to_bytes(3, 'big') + data
    checksum = (~sum(body)) & 0xFF
    return f'S2{count:02X}{address:06X}{data.hex().upper()}{checksum:02X}\r\n'.encode()


def build_main_upd(main: bytes, *, main_ver: bytes) -> bytes:
    out = bytearray(b'XDJ-700     MAIN' + main_ver + b'\0       0')
    out += b'S00E0000726F6D6F626A20206D6F74D8\r\n'
    address = 0
    while address < len(main):
        if any(start <= address < end for start, end in GAPS):
            address += 32
            continue
        chunk = main[address:address + 32]
        out += s2(address, chunk)
        address += len(chunk)
    out += b'S705A00000005A\r\n'
    return append_crc16_le(bytes(out))


def build_panel_upd(panel: bytes, *, source_upd_bytes: bytes) -> bytes:
    # Reuse only record boundaries from the exact owner-supplied official UPD.
    lines = source_upd_bytes.split(b'\n')
    source_records = [line for line in lines if line[:2] == b'S2'][222510:]
    out = bytearray(b'XDJ-700     PANLVer1.00         S0030000FC\r\n')
    for source in source_records:
        line = source.strip(b'\r')
        count = int(line[2:4], 16)
        address = int(line[4:10], 16)
        data_len = count - 4
        data = (panel[address - 0x0C0000:address - 0x0C0000 + data_len]
                if address >= 0x0C0000 else bytes.fromhex(line[10:10 + data_len * 2].decode()))
        out += s2(address, data)
    out += b'S804000000FB\r\n'
    return append_crc16_le(bytes(out))


def build_upd(main: bytes, panel: bytes, *, main_ver: bytes,
              source_upd_bytes: bytes) -> bytes:
    main_doc = build_main_upd(main, main_ver=main_ver)
    panel_doc = build_panel_upd(panel, source_upd_bytes=source_upd_bytes)
    lengths = f'{len(main_doc)}\r\n{len(panel_doc)}\r\n'.encode()
    return lengths + main_doc + panel_doc
