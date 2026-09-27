#!/usr/bin/env python3
"""Owner-input tests for the fixed alpha.1 patcher (no USB/player access)."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

# Allow direct execution from any working directory.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import patcher
from lzss_pioneer import decode_section


def rejected(arguments: list[str], output: Path | None = None) -> None:
    result = subprocess.run([sys.executable, '-B', str(Path(patcher.__file__)), *arguments],
                            capture_output=True, text=True)
    assert result.returncode != 0, result.stdout
    if output is not None:
        assert not output.exists(), f'unexpected output: {output}'


def run(official_path: Path, *, full: bool) -> None:
    official = official_path.read_bytes()
    patcher.pinned(official, patcher.STOCK_UPD, 'official UPD')
    main, panl = patcher.stock_images(official)
    assert patcher.pinned(main, patcher.STOCK_MAIN, 'MAIN') == main
    assert patcher.pinned(panl, patcher.STOCK_PANL, 'PANL') == panl
    decoded = decode_section(main, 0x40000)
    recipe = patcher.RECIPE.read_bytes()
    patcher.pinned(recipe, (patcher.RECIPE_BYTES, patcher.RECIPE_SHA), 'template')
    import json
    assembled = patcher.apply_recipe(decoded, json.loads(recipe))
    assert (len(assembled), patcher.sha(assembled)) == patcher.TARGET_DECODED

    with tempfile.TemporaryDirectory(prefix='xdj700-patcher-test-') as temporary:
        root = Path(temporary)
        target = root / 'XDJ700.UPD'
        altered = root / 'altered.UPD'
        altered.write_bytes(official[:100] + bytes((official[100] ^ 1,)) + official[101:])
        rejected(['--official-upd', str(altered), '--output', str(target)], target)
        truncated = root / 'truncated.UPD'
        truncated.write_bytes(official[:-1])
        rejected(['--official-upd', str(truncated), '--output', str(target)], target)
        extended = root / 'extended.UPD'
        extended.write_bytes(official + b'\0')
        rejected(['--official-upd', str(extended), '--output', str(target)], target)
        rejected(['--official-upd', str(official_path), '--output', str(official_path)])
        target.write_bytes(b'KEEP')
        rejected(['--official-upd', str(official_path), '--output', str(target)])
        assert target.read_bytes() == b'KEEP'
        target.unlink()
        local_official = root / 'official-copy.UPD'
        local_official.write_bytes(official)
        os.link(local_official, target)
        rejected(['--official-upd', str(official_path), '--output', str(target)])
        assert target.read_bytes() == official
        target.unlink()
        try:
            target.symlink_to(official_path)
        except (OSError, NotImplementedError):
            pass
        else:
            rejected(['--official-upd', str(official_path), '--output', str(target)])
            target.unlink()

        copied = root / 'copy'
        copied.mkdir()
        for name in ('patcher.py', 'srecord_update.py', 'lzss_pioneer.py',
                     'flac_alpha1_template.json'):
            shutil.copy2(Path(patcher.__file__).parent / name, copied / name)
        template = copied / 'flac_alpha1_template.json'
        data = template.read_bytes()
        template.write_bytes(data[:-2] + bytes((data[-2] ^ 1,)) + data[-1:])
        result = subprocess.run([sys.executable, '-B', str(copied / 'patcher.py'),
                                 '--official-upd', str(official_path), '--output', str(target)],
                                capture_output=True, text=True)
        assert result.returncode != 0 and not target.exists()

        if full:
            result = subprocess.run([sys.executable, '-B', str(Path(patcher.__file__)),
                                     '--official-upd', str(official_path), '--output', str(target)],
                                    capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            assert patcher.pinned(target.read_bytes(), patcher.TARGET_UPD, 'created UPD')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--official-upd', type=Path, required=True)
    parser.add_argument('--full', action='store_true', help='also run the full LZSS rebuild')
    args = parser.parse_args()
    run(args.official_upd, full=args.full)
    print('PASS: exact source/reconstruction and refusal cases' +
          (' plus full package build' if args.full else ''))
