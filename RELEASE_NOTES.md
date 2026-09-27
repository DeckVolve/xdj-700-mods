# XDJ-700 FLAC support — v0.1.0-alpha.1

First experimental FLAC release from DeckVolve. This release supplies source
and an owner-input patcher, not a firmware download. The patcher requires the
exact official XDJ-700 v1.15 update and creates the modified updater locally.

## What worked in development tests

- FLAC loading and playback on the owner's XDJ-700, including pause, replay,
  cueing and repeated loads.
- A FLAC track exported through rekordbox appeared, loaded and played through
  the Device Library route.
- The fixed template was reproduced from source. The Linux patcher passed
  full output-hash verification and changed-input/overwrite refusal checks.

Those player results concern the same application code in earlier development
containers. They do not qualify every FLAC encoding, library mode or device.

## Known issues and limits

- **AAC/M4A regression:** a tested AAC-LC track loads slowly and leaves the
  player sluggish, including when loaded first after boot. The exact same file
  and restored export work normally on official v1.15. Avoid AAC/M4A in this
  alpha. An AAC fix is not included.
- Linux is the validated patcher platform. Windows and macOS are unvalidated.
- ALAC, new library formats and other modifications are not included.
- This is not a reliability-qualified release for live use.

Read [README.md](README.md) before creating or installing an update. Keep stable
power throughout a firmware write. A failed write can leave the player unusable.

## Distribution and licensing

The release archive contains the patcher, sparse template, complete selected
source, build instructions and dependency notices. Official firmware, generated
updaters, owner media, test captures and research Git history are excluded.

Project-authored code is MIT, attributed to DeckVolve contributors. libFLAC's BSD
and MD5 notices and the GCC runtime exception remain intact; see
[LICENSING.md](LICENSING.md). The source packaging/attribution update changes no
firmware bytes or behavior from the previously prepared alpha.1 patcher.
