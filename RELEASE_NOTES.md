# XDJ-700 FLAC support — v0.1.0-alpha.2

Experimental FLAC release from DeckVolve. This release supplies source
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

- **AAC/M4A slow loading fixed:** bounded owner testing found fast first M4A
  loading, normal short FLAC playback/pause and fast return to M4A.
- Later AAC/M4A testing reported UI sluggishness. Its cause is
  unresolved; an alpha.2 UI regression is not established and stock-like UI
  responsiveness, full-track playback, seeking and every browse route are not
  qualified.
- The exact owner-tested updater reports MAIN 1.22 / Utility 0.96. Official
  v1.15 restore acceptance from this version has not been qualified.
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
[LICENSING.md](LICENSING.md). The authored 76-byte reader-release helper releases the dispatch reader before
native source-reply publication. FLAC decoder and payload bytes are unchanged.
