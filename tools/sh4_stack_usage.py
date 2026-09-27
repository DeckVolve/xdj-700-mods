#!/usr/bin/env python3
"""Byte-bind GCC stack reports to libFLAC archives and audit linked C frames.

Static frames are not cumulative call-chain or interrupt-stack bounds. Keep
assembly/compiler-runtime symbols visible as a separate review obligation.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess


# Explicitly reviewed non-C veneers and GCC 14.3 SH runtime exports. Do not
# silently classify a newly missing C report as an assembly exemption.
UNREPORTED_SYMBOLS = frozenset((
    "__ashldi3", "__ashrdi3", "__lshrdi3", "__movmemSI12_i4",
    "__movmem_i4_even", "__movmem_i4_odd", "__movstrSI12_i4",
    "__movstr_i4_even", "__movstr_i4_odd", "__sdivsi3_i4i",
    "__udiv_qrnnd_16", "__udivdi3", "__udivsi3_i4i",
    "stock_lossless_dispatch_trampoline", "stock_lossless_read_entry",
    "stock_lossless_read_original",
))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def parse_report(text):
    result = {}
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            raise ValueError("malformed GCC stack report")
        source, count, qualifier = fields
        name = source.rsplit(":", 1)[-1]
        if qualifier != "static" or int(count) < 0 or name in result:
            raise ValueError(f"unbounded or duplicate stack entry: {name}")
        result[name] = int(count)
    if not result:
        raise ValueError("empty GCC stack report")
    return result


def record_archive(directory, ar):
    directory = Path(directory)
    archive = directory / "libFLAC.a"
    units = {}
    members = subprocess.check_output([str(ar), "t", str(archive)], text=True).splitlines()
    if len(set(members)) != len(members):
        raise ValueError("duplicate archive members")
    for member in members:
        data = subprocess.check_output([str(ar), "p", str(archive), member])
        report = (directory / member).with_suffix(".su").read_text()
        units[member] = {"object_sha256": digest(data), "report": report,
                         "frames": parse_report(report) if report else {}}
    record = {"schema": 1, "archive_sha256": digest(archive.read_bytes()), "units": units}
    (directory / "libFLAC.stack.json").write_text(json.dumps(record, indent=2, sort_keys=True)+"\n")


def load_archive_report(archive, report, ar):
    record = json.loads(report.read_text())
    if record.get("schema") != 1 or record.get("archive_sha256") != digest(archive.read_bytes()):
        raise ValueError("libFLAC stack evidence does not match the linked archive")
    members = subprocess.check_output([str(ar), "t", str(archive)], text=True).splitlines()
    if set(record["units"]) != set(members) or len(set(members)) != len(members):
        raise ValueError("libFLAC stack evidence member set differs")
    result = {}
    for member in members:
        unit = record["units"][member]
        data = subprocess.check_output([str(ar), "p", str(archive), member])
        frames = parse_report(unit["report"]) if unit["report"] else {}
        if digest(data) != unit["object_sha256"] or frames != unit["frames"]:
            raise ValueError(f"libFLAC object/report identity differs: {member}")
        result["libFLAC/" + member] = frames
    # Data-only units legitimately emit empty reports. Every archive text
    # symbol must nevertheless have a report, so an empty/truncated .su for
    # a code unit cannot silently become an assembly exemption.
    nm = Path(ar).with_name(Path(ar).name.removesuffix("ar") + "nm")
    symbols = subprocess.check_output([str(nm), "-P", "--defined-only", str(archive)], text=True)
    names = {normalized(n) for frames in result.values() for n in frames}
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[1] in ("t", "T") and normalized(fields[0]) not in names:
            raise ValueError(f"libFLAC text symbol has no stack report: {fields[0]}")
    return result


def normalized(name):
    # GCC .su omits the numeric suffix of some IPA clone names.
    return re.sub(r"\.(isra|constprop|part)\.\d+$", r".\1", name)


def audit_linked(nm_output, units, limit):
    by_name = {}
    for unit, frames in units.items():
        for name, count in frames.items():
            by_name.setdefault(normalized(name), []).append((unit, count))
    linked, other = {}, []
    for line in nm_output.splitlines():
        fields = line.split()
        if len(fields) != 4 or fields[2] not in ("t", "T"):
            continue
        name = fields[3]
        found = by_name.get(normalized(name))
        if found is None:
            other.append(name)
            continue
        count = max(n for _, n in found)
        if count > limit:
            raise ValueError(f"linked SH-4 C frame exceeds {limit} bytes: {name}={count}")
        linked[name] = {"bytes": count, "reports": sorted(u for u, _ in found)}
    if "read_frame_" not in linked:
        raise ValueError("linked FLAC read_frame_ has no stack evidence")
    unknown = set(other) - UNREPORTED_SYMBOLS
    if unknown:
        raise ValueError("linked symbol has no stack report or explicit exemption: " +
                         ", ".join(sorted(unknown)))
    return {"scope": "linked C static frames; excludes cumulative/native/interrupt bounds",
            "limit": limit, "largest": max(v["bytes"] for v in linked.values()),
            "linked_c_frames": linked, "assembly_or_runtime_symbols": sorted(other)}


if __name__ == "__main__":
    import sys
    record_archive(sys.argv[1], sys.argv[2])
