# DeckVolve — XDJ-700 mods

FLAC support for the Pioneer DJ XDJ-700. This first release, **v0.1.0-alpha.1**,
includes a patcher and source code. You supply the official firmware; no
firmware is distributed here. ALAC is not included.

**Known issue:** AAC/M4A can load slowly and make the player sluggish. Avoid
AAC/M4A in this alpha, and do not rely on it for live performances.
See the [release notes](RELEASE_NOTES.md) for tested behavior and limitations.

## Create the update

You need **Python 3.10+** and the official **XDJ-700 v1.15** firmware from the
[manufacturer's download page](https://www.pioneerdj.com/en/support/software/player/xdj-700/).
Extract `XDJ700.UPD` outside this repository.

Create a separate output folder on your computer's local disk, then run:

**Linux / macOS**

```sh
python3 patcher.py --official-upd /path/to/official/XDJ700.UPD --output /path/to/new/XDJ700.UPD
```

**Windows**

```powershell
py -3 patcher.py --official-upd C:\path\to\official\XDJ700.UPD --output C:\path\to\new\XDJ700.UPD
```

The patcher verifies the input and output automatically and never overwrites
an existing file. Allow a few minutes. Generate the file locally, not directly
on a FAT32/exFAT USB stick; the output filesystem must support hard links.
Linux is tested; Windows and macOS are not yet validated.

## Install

Use the manufacturer's update guide from the download page. Keep stable power
and leave the USB connected until the update finishes. A failed write can leave
the player unusable, and recovery is not guaranteed.

The updater reports **1.16**; Utility reports **1.14**.

## Development

[Build from source and run the tests](BUILD_FROM_SOURCE.md).

## License

Project code is [MIT](LICENSE), attributed to DeckVolve contributors.
[Third-party licenses and notices](LICENSING.md) remain applicable.
The license does not cover official firmware or its redistribution.

Unofficial project, not affiliated with or endorsed by Pioneer DJ or AlphaTheta.
