#!/usr/bin/env python3
"""Build a source-only release from an explicit allowlist, never from the working tree wholesale."""
from pathlib import Path
import hashlib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FILES = [
    '.gitignore', 'Data-Tools.py', 'README.md', 'THIRD_PARTY.md', 'requirements.txt',
    'data_tools/__init__.py', 'data_tools/core.py', 'data_tools/cli.py',
    'data_tools/licensing.py', 'data_tools/sources.py', 'data_tools/report.html',
    'examples/demo.html', 'examples/public_records.csv',
    'tests/test_app.py', 'tests/test_ui.cjs', 'tools/package.py',
]


def main():
    manifest = ''.join(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() + '  ' + name + '\n' for name in FILES)
    archive = ROOT / 'Data-Tools-1.0.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as output:
        for name in FILES:
            output.write(ROOT / name, 'Data-Tools/' + name)
        output.writestr('Data-Tools/SHA256SUMS.txt', manifest)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix('.zip.sha256').write_text(digest + '  ' + archive.name + '\n', encoding='ascii')
    print(f'{archive.name}: {archive.stat().st_size} bytes\nSHA256: {digest}')


if __name__ == '__main__':
    main()
