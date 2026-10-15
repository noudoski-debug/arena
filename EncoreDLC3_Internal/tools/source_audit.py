#!/usr/bin/env python3
"""Record which exact Encore archive source units were retained or replaced for internal."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = ROOT.parent / 'EncoreDLC3_ESP_10FPS_source_fix.zip'
EXPECTED = '86ab386576bd55d953d3a9ed0a97e337bd15ed7bd1298bf55995f28385d5b3ec'
MAPPING = {
    'jni/src/main.cpp': ['jni/src/Main.cpp'],
    'jni/src/RemotePatch.cpp': ['jni/src/Runtime.cpp'],
    'jni/src/Esp.cpp': ['jni/src/Runtime.cpp', 'jni/src/Menu.cpp'],
    'jni/src/Aim.cpp': ['jni/src/Runtime.cpp', 'jni/include/ModuleMath.h'],
    'jni/src/FreeCam.cpp': ['jni/src/Runtime.cpp', 'jni/src/Menu.cpp'],
    'jni/src/Bind.cpp': ['jni/src/Input.cpp'],
    'jni/src/Vars.cpp': ['jni/src/Config.cpp', 'jni/src/Menu.cpp'],
    'jni/include/Vars.h': ['jni/include/Vars.h'],
    'jni/src/LitvinMenu.h': ['jni/include/encore/LitvinMenu.h'],
    'jni/include/HitEffect.h': ['jni/include/encore/HitEffect.h'],
    'jni/src/Chams.h': ['jni/include/Chams.h'],
    'jni/src/Chams.cpp': ['jni/src/Chams.cpp'],
    'jni/Android.mk': ['jni/Android.mk'],
    'jni/Application.mk': ['jni/Application.mk'],
    'run-root.sh': ['run-root.sh'],
    'build.sh': ['build.sh'],
}

def audit():
    digest = hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()
    if digest != EXPECTED:
        raise ValueError('The source archive is not the requested ESP_10FPS_source_fix revision')
    units = []
    with zipfile.ZipFile(ARCHIVE) as archive:
        for original, replacements in MAPPING.items():
            data = archive.read('EncoreDLC3_fixed/' + original)
            for path in replacements:
                if not (ROOT / path).is_file():
                    raise ValueError(f'Missing internal implementation: {path}')
            retained = len(replacements) == 1 and data == (ROOT / replacements[0]).read_bytes()
            units.append({'original': original, 'original_sha256': hashlib.sha256(data).hexdigest(),
                          'internal': replacements, 'change': 'retained verbatim' if retained else 'adapted / replaced'})
    mk = (ROOT / 'jni/Android.mk').read_text()
    if 'LOCAL_MODULE := Encore\n' not in mk or 'include $(BUILD_SHARED_LIBRARY)' not in mk:
        raise ValueError('Game payload must be an internal libEncore.so')
    forbidden = ('RemotePatch.cpp', 'Android_draw/draw.cpp', 'Android_touch/Touch.cpp', 'src_internal/', 'src_inject/')
    if any(token in mk for token in forbidden):
        raise ValueError('External overlay/collector backend is still in the active build')
    for name in ('Main.cpp', 'Runtime.cpp', 'Menu.cpp', 'Input.cpp'):
        text = (ROOT / 'jni/src' / name).read_text()
        if any(token in text for token in ('process_vm_readv', 'process_vm_writev', '/dev/input', 'esp.bin', 'ANativeWindowCreator')):
            raise ValueError(f'External backend dependency in {name}')
    return {'source_archive': ARCHIVE.name, 'sha256': digest,
            'payload': 'libEncore.so', 'loader': 'encore-loader',
            'device_tested': False, 'all_features_complete': False,
            'shared_dependencies': 'Dobby and newer ImGui from the existing internal scaffold; see THIRD_PARTY.md',
            'source_units': units}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true')
    args = parser.parse_args()
    text = json.dumps(audit(), ensure_ascii=False, indent=2) + '\n'
    output = ROOT / 'SOURCE_MAP.json'
    if args.write:
        output.write_text(text)
    elif not output.exists() or output.read_text() != text:
        raise SystemExit('SOURCE_MAP.json differs; review source and run --write')
    print('PASS: exact Encore ESP_10FPS archive / source mapping / internal-only payload build')

if __name__ == '__main__':
    main()
