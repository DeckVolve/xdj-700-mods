"""Pioneer XDJ-700 MAIN firmware LZSS codec.

Semantics are verified instruction-by-instruction against the device's
decompressor at flash 0x000900-0x0009C2 (see RESEARCH_NOTES.md).  The stock
copy loop stores ``(b2 & 0xf) + 2`` in its counter and then performs the
inclusive compare before the delayed ``bf/s``; consequently it emits one
more byte, ``(b2 & 0xf) + 3``.  The maximum token length is therefore 18.

Container: [u32 comp_size]["01 00"]["EE FF"] <stream>, where ``comp_size``
includes the four-byte tag.  The stock loader passes the tag as the first
input byte, so :func:`decode_section` starts at ``base + 4``.

The stored section is followed by a little-endian 16-bit additive checksum of
the size field plus compressed bytes.  The reset path validates the large
application checksum before selecting it; a mismatch selects the smaller
fallback updater section.

The encoder is a deterministic greedy encoder for the same format.  It is
intended for rebuilding a modified decompressed MAIN image; it is not a
claim that the stock packer used this exact match-selection heuristic.
"""

from collections import defaultdict
import struct


WINDOW_SIZE = 4096
WINDOW_MASK = WINDOW_SIZE - 1
INITIAL_WRITE_INDEX = WINDOW_SIZE - 18
MAX_MATCH = 18
MIN_MATCH = 3
SECTION_TAG = b"\x01\x00\xEE\xFF"
SECTION_CHECKSUM_SIZE = 2


def section_data_end(image: bytes, base: int) -> int:
    """Return the exclusive end of a stored section's size+compressed data."""

    if base < 0 or base + 4 > len(image):
        raise ValueError("section size field is outside the image")
    size = struct.unpack_from("<I", image, base)[0]
    end = base + 4 + size
    if end > len(image):
        raise ValueError("section compressed data is truncated")
    return end


def calculate_section_checksum(image: bytes, base: int = 0) -> int:
    """Calculate the boot loader's additive checksum over one section.

    The checksum covers the little-endian size field and every compressed byte,
    then is stored as a little-endian 16-bit value immediately afterward.
    """

    end = section_data_end(image, base)
    return sum(image[base:end]) & 0xFFFF


def stored_section_checksum(image: bytes, base: int = 0) -> int:
    """Read the little-endian checksum immediately after compressed data."""

    end = section_data_end(image, base)
    if end + SECTION_CHECKSUM_SIZE > len(image):
        raise ValueError("section checksum is truncated")
    return struct.unpack_from("<H", image, end)[0]


def validate_section_checksum(image: bytes, base: int = 0) -> int:
    """Validate and return a section checksum."""

    calculated = calculate_section_checksum(image, base)
    stored = stored_section_checksum(image, base)
    if stored != calculated:
        raise ValueError(
            f"section checksum mismatch: stored=0x{stored:04X}, "
            f"calculated=0x{calculated:04X}"
        )
    return stored


def append_section_checksum(section: bytes) -> bytes:
    """Append the boot loader checksum to an exact encoded section."""

    end = section_data_end(section, 0)
    if end != len(section):
        raise ValueError("encoded section contains trailing bytes")
    checksum = calculate_section_checksum(section, 0)
    return section + struct.pack("<H", checksum)

def lzss_decode(stream: bytes) -> bytes:
    ring = bytearray([0x20]) * WINDOW_SIZE  # fill loop @0x922
    wr = INITIAL_WRITE_INDEX                # initial ring write index
    out = bytearray()

    def emit(b: int):
        nonlocal wr
        out.append(b)
        ring[wr & WINDOW_MASK] = b
        wr += 1

    i, n = 0, len(stream)
    while i < n:
        flags = stream[i]; i += 1
        for _ in range(8):          # 8 items per flag byte, LSB first
            if i >= n:
                return bytes(out)
            if flags & 1:           # bit set -> literal (@0x958 path)
                emit(stream[i]); i += 1
            else:                   # bit clear -> match (@0x96E path)
                b1 = stream[i]; b2 = stream[i + 1]; i += 2
                pos = b1 | ((b2 >> 4) << 8)   # r14 = extu.b(b1) | ((b2&0xF0)<<4)
                # The firmware sets r12=(b2&15)+2, increments r7 before the
                # compare, and leaves the compare's delay slot in the copy
                # loop.  It consequently emits r12+1 bytes.
                length = (b2 & 0xF) + 3
                for k in range(length):       # copy loop @0x996, src advances
                    emit(ring[(pos + k) & WINDOW_MASK])
            flags >>= 1
    return bytes(out)


def _match_byte(data: bytes, current: int, source: int, offset: int) -> int:
    """Return a byte as the device's overlapping ring copy would produce.

    ``source`` and ``current`` are logical output positions.  Positions before
    zero refer to the decompressor's space-filled history.  Once a match
    overlaps the current output, the source byte comes from the already copied
    prefix of that same match.
    """

    distance = current - source
    logical = source + offset
    if logical < current:
        return 0x20 if logical < 0 else data[logical]
    return data[current + (offset - distance)]


def _match_length(data: bytes, current: int, source: int) -> int:
    distance = current - source
    if not 1 <= distance <= WINDOW_SIZE:
        return 0
    limit = min(MAX_MATCH, len(data) - current)
    length = 0
    while length < limit:
        if _match_byte(data, current, source, length) != data[current + length]:
            break
        length += 1
    return length


def _encode_from_state(
    data: bytes,
    ring: bytearray,
    wr: int,
    current: int,
    stream: bytearray,
    flag_offset: int | None,
    bit: int,
) -> bytes:
    """Encode from an already initialized decoder state.

    ``current`` is the logical output position represented by ``wr``.  The
    section container has four fixed input bytes which already produce a
    19-byte zero prefix, so the section encoder uses this helper after
    replaying those bytes.
    """

    # A three-byte index avoids a 4096-position scan for every token.  Older
    # candidates are still retained until the query-time window check; this
    # keeps the implementation simple and makes the encoded output stable.
    buckets: dict[bytes, list[int]] = defaultdict(list)
    for source in range(-WINDOW_SIZE, -2):
        buckets[b"   "].append(source)

    # The two history positions that cross the logical zero boundary are not
    # all-spaces once the first input bytes have been emitted.
    indexed_until = 0
    history_indexed = set()

    def add_known_candidates(limit: int) -> None:
        nonlocal indexed_until
        while indexed_until + 2 < limit:
            source = indexed_until
            buckets[data[source:source + 3]].append(source)
            indexed_until += 1
        # These entries are useful for files beginning with padding/spaces.
        for source in (-2, -1):
            if source in history_indexed or source + 2 >= limit:
                continue
            key = bytes(_match_byte(data, limit, source, k) for k in range(3))
            buckets[key].append(source)
            history_indexed.add(source)

    def best_match(current: int) -> tuple[int, int]:
        add_known_candidates(current)
        if current + MIN_MATCH > len(data):
            return 0, 0
        key = data[current:current + 3]
        candidates = buckets.get(key, ())
        best_len = 0
        best_source = 0

        # The newest candidates tend to be the best for the highly repetitive
        # resource tables in this firmware.  A bounded reverse scan keeps
        # packing the 18.6 MiB application practical while still considering
        # enough history for less repetitive code/data.
        max_candidates = 4096
        lower_bound = current - WINDOW_SIZE
        for source in reversed(candidates[-max_candidates:]):
            if source < lower_bound or source >= current:
                continue
            length = _match_length(data, current, source)
            if length > best_len:
                best_len = length
                best_source = source
                if length == MAX_MATCH:
                    break

        # Distances 1 and 2 cannot have a non-overlapping three-byte index,
        # but they are common in runs of one or two repeated bytes.
        for distance in (1, 2):
            source = current - distance
            if source < lower_bound:
                continue
            length = _match_length(data, current, source)
            if length > best_len:
                best_len = length
                best_source = source
        return best_len, best_source

    while current < len(data):
        if flag_offset is None:
            flag_offset = len(stream)
            stream.append(0)
            bit = 0
        flags = stream[flag_offset]

        length, source = best_match(current)
        if length >= MIN_MATCH:
            position = (INITIAL_WRITE_INDEX + source) & WINDOW_MASK
            stream.append(position & 0xFF)
            stream.append(((position >> 8) << 4) | (length - 3))
            for k in range(length):
                value = ring[(position + k) & WINDOW_MASK]
                if value != data[current + k]:
                    raise AssertionError("LZSS match selection produced bad output")
                ring[wr & WINDOW_MASK] = value
                wr += 1
            current += length
        else:
            flags |= 1 << bit
            value = data[current]
            stream.append(value)
            ring[wr & WINDOW_MASK] = value
            wr += 1
            current += 1

        bit += 1
        stream[flag_offset] = flags
        if bit == 8:
            flag_offset = None
            bit = 0
    return bytes(stream)


def lzss_encode(data: bytes) -> bytes:
    """Greedily encode *data* using the XDJ-700's LZSS token format.

    The device stores an absolute 12-bit ring position rather than a distance.
    The logical source position used here therefore maps to
    ``(INITIAL_WRITE_INDEX + source) & 4095``.  The index keeps only a bounded
    set of three-byte candidates; the resulting stream is deterministic and
    is validated by the decoder in the test suite.

    This function encodes a standalone token stream.  Firmware sections use
    :func:`encode_section`, whose fixed tag is itself consumed by the stock
    decoder and therefore needs a seeded encoder state.
    """

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("data must be bytes-like")
    data = bytes(data)
    if not data:
        return b""
    return _encode_from_state(
        data,
        bytearray([0x20]) * WINDOW_SIZE,
        INITIAL_WRITE_INDEX,
        0,
        bytearray(),
        None,
        0,
    )


def encode_section(data: bytes) -> bytes:
    """Return a complete stock ``[size][tag+stream]`` compressed section.

    The fixed tag is not an ignored header: the stock decoder consumes it as
    the first flag/literal/match items.  As a result, a section's decoded
    bytes must begin with the tag's 19-byte output prefix (currently zeroes).
    """

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("data must be bytes-like")
    data = bytes(data)

    ring = bytearray([0x20]) * WINDOW_SIZE
    wr = INITIAL_WRITE_INDEX

    # Replay SECTION_TAG exactly as the firmware does: flag 0x01 selects one
    # literal (0x00), followed by a low-nibble-0xF match at position 0xFEE.
    value = SECTION_TAG[1]
    ring[wr & WINDOW_MASK] = value
    wr += 1
    position = SECTION_TAG[2] | ((SECTION_TAG[3] >> 4) << 8)
    seed_length = (SECTION_TAG[3] & 0xF) + 3
    for k in range(seed_length):
        value = ring[(position + k) & WINDOW_MASK]
        ring[wr & WINDOW_MASK] = value
        wr += 1

    seed = b"\x00" * (1 + seed_length)
    if not data.startswith(seed):
        raise ValueError(
            "stock section data must begin with the 19-byte zero prefix "
            "produced by the fixed 01 00 EE FF tag"
        )

    stream = bytearray(SECTION_TAG)
    _encode_from_state(data, ring, wr, len(seed), stream, 0, 2)
    return struct.pack("<I", len(stream)) + bytes(stream)


def decode_section(image: bytes, base: int) -> bytes:
    size = struct.unpack('<I', image[base:base+4])[0]
    return lzss_decode(image[base+4:base+4+size])
