#!/usr/bin/env python3
"""Generate/check the INTERNAL RVA table directly from dump.cs (plain or ZIP)."""
import argparse
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
METHODS = {
    'PhysicsLinecast': ('UnityEngine', 'Physics', 'public static bool Linecast(Vector3 start, Vector3 end, out RaycastHit hitInfo, int layerMask, QueryTriggerInteraction queryTriggerInteraction)'),
    'HitTransform': ('UnityEngine', 'RaycastHit', 'public Transform get_transform()'),
    'IsChildOf': ('UnityEngine', 'Transform', 'public bool IsChildOf(Transform parent)'),
    'PlayerLateUpdate': ('Oxide', 'PlayerManager', 'private void LateUpdate()'),
    'WeaponUpdate': ('Oxide', 'FPHitscan', 'private void Update()'),
    'FpLateUpdate': ('Oxide', 'FPManager', 'private void LateUpdate()'),
    'SetPosition': ('UnityEngine', 'Transform', 'public void set_position(Vector3 value)'),
    'GetRotation': ('UnityEngine', 'Transform', 'public Quaternion get_rotation()'),
    'SetRotation': ('UnityEngine', 'Transform', 'public void set_rotation(Quaternion value)'),
    'GetForward': ('UnityEngine', 'Transform', 'public Vector3 get_forward()'),
    'GetRight': ('UnityEngine', 'Transform', 'public Vector3 get_right()'),
    'GetFov': ('UnityEngine', 'Camera', 'public float get_fieldOfView()'),
    'SetFov': ('UnityEngine', 'Camera', 'public void set_fieldOfView(float value)'),
    'GetAspect': ('UnityEngine', 'Camera', 'public float get_aspect()'),
    'SetAspect': ('UnityEngine', 'Camera', 'public void set_aspect(float value)'),
    'WorldToScreenPoint': ('UnityEngine', 'Camera', 'public Vector3 WorldToScreenPoint(Vector3 position)'),
    'GetPosition': ('UnityEngine', 'Transform', 'public Vector3 get_position()'),
    'GetTransform': ('UnityEngine', 'Component', 'public Transform get_transform()'),
    'GetMainCamera': ('UnityEngine', 'Camera', 'public static Camera get_main()'),
    'GetScreenWidth': ('UnityEngine', 'Screen', 'public static int get_width()'),
    'GetScreenHeight': ('UnityEngine', 'Screen', 'public static int get_height()'),
    'IsLocalPlayer': ('Mirror', 'NetworkBehaviour', 'public bool get_isLocalPlayer()'),
    'PlayerUpdate': ('Oxide', 'PlayerManager', 'private void Update()'),
    'PlayerOnDisable': ('Oxide', 'PlayerManager', 'private void OnDisable()'),
}
FIELDS = {
    'HitPoint': ('UnityEngine', 'RaycastHit', 'm_Point'),
    'HitNormal': ('UnityEngine', 'RaycastHit', 'm_Normal'),
    'HitFace': ('UnityEngine', 'RaycastHit', 'm_FaceID'),
    'HitDistance': ('UnityEngine', 'RaycastHit', 'm_Distance'),
    'HitUv': ('UnityEngine', 'RaycastHit', 'm_UV'),
    'HitCollider': ('UnityEngine', 'RaycastHit', 'm_Collider'),
    'EntityData': ('UnityEngine', 'EntityId', 'm_Data'),
    'WorldCameraRoot': ('Oxide', 'PlayerManager', 'worldCameraRoot'),
    'MouseLook': ('Oxide', 'PlayerManager', 'mouseLook'),
    'LookAngle': ('Oxide', 'PlayerManager', 'lookAngle'),
    'LastTickPosition': ('Oxide', 'PlayerManager', 'lastTickPosition'),
    'FpManager': ('Oxide', 'PlayerManager', 'fpManager'),
    'WorldCamera': ('Oxide', 'FPManager', 'm_WorldCamera'),
    'CurrentWeapon': ('Oxide', 'FPManager', '_currentWeapon'),
    'ScopeBlend': ('Oxide', 'FPManager', '<qkr>k__BackingField'),
    'MouseAngles': ('Oxide', 'MouseLook', 'qxz'),
    'WeaponPlayer': ('Oxide', 'FPObject', 'Player'),
    'NormalFov': ('Oxide', 'FPObject', 'normalFOV'),
    'AimFov': ('Oxide', 'FPObject', 'aimFOV'),
    'WeaponConfig': ('Oxide', 'FPHitscan', 'config'),
    'RecoilConfig': ('HyperHug.Games.Oxide.Features.Weapons', 'HitscanWeaponConfig', 'recoilConfig'),
    'DispersionConfig': ('HyperHug.Games.Oxide.Features.Weapons', 'HitscanWeaponConfig', 'dispersionConfig'),
    'MinRecoil': ('HyperHug.Games.Oxide.Features.Weapons', 'RecoilConfig', '<MinRecoil>k__BackingField'),
    'MaxRecoil': ('HyperHug.Games.Oxide.Features.Weapons', 'RecoilConfig', '<MaxRecoil>k__BackingField'),
    'MinFireDispersion': ('HyperHug.Games.Oxide.Features.Weapons', 'DispersionConfig', 'minFireDispersion'),
    'MaxFireDispersion': ('HyperHug.Games.Oxide.Features.Weapons', 'DispersionConfig', 'maxFireDispersion'),
    'MinDispersion': ('HyperHug.Games.Oxide.Features.Weapons', 'DispersionConfig', 'minDispersion'),
    'MaxDispersion': ('HyperHug.Games.Oxide.Features.Weapons', 'DispersionConfig', 'maxDispersion'),
    'DispersionIncrease': ('HyperHug.Games.Oxide.Features.Weapons', 'DispersionConfig', 'dispersionIncreasePerShot'),
    'Respawning': ('Oxide', 'PlayerManager', 'respawning'),
    'Kicked': ('Oxide', 'PlayerManager', 'kicked'),
    'UserId': ('Oxide', 'PlayerManager', 'userID'),
}

def read_dump(path):
    if path.suffix.lower() == '.zip':
        with zipfile.ZipFile(path) as archive:
            members = [n for n in archive.namelist() if Path(n).name == 'dump.cs']
            if len(members) != 1:
                raise ValueError('ZIP must contain exactly one dump.cs')
            data = archive.read(members[0])
    else:
        data = path.read_bytes()
    return data.decode('utf-16' if data[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig')

def class_block(text, namespace, name):
    found = []
    for section in re.split(r'^// Namespace: ', text, flags=re.M)[1:]:
        ns, _, body = section.partition('\n')
        if ns.strip() != namespace:
            continue
        if re.search(r'^public (?:sealed |abstract |readonly )?(?:class|struct) ' + re.escape(name) + r'(?:\s|:)', body, re.M):
            found.append(body)
    if len(found) != 1:
        raise ValueError(f'{namespace}.{name}: expected one class, found {len(found)}')
    return found[0]

def method_rva(block, signature):
    matches = []
    pending = None
    for line in block.splitlines():
        m = re.search(r'// RVA: (0x[0-9A-Fa-f]+|-1) Offset:', line)
        if m:
            pending = m.group(1)
        if line.strip().startswith(signature + ' {'):
            if pending is None or pending == '-1':
                raise ValueError(f'{signature}: missing concrete RVA')
            matches.append(int(pending, 16))
        if re.search(r'\)\s*\{', line):
            pending = None
    if len(matches) != 1:
        raise ValueError(f'{signature}: expected one method, found {len(matches)}')
    return matches[0]

def field_offset(block, name):
    fields = block.split('// Properties')[0].split('// Methods')[0]
    hits = re.findall(r'(?<!\w)' + re.escape(name) + r';\s*// (0x[0-9A-Fa-f]+)', fields)
    if len(hits) != 1:
        raise ValueError(f'{name}: expected one field, found {len(hits)}')
    return int(hits[0], 16)

def render(text):
    cache = {}
    def block(ns, cls):
        if (ns, cls) not in cache:
            cache[ns, cls] = class_block(text, ns, cls)
        return cache[ns, cls]
    lines = ['#pragma once', '#include <cstdint>',
             '// Generated from dump 1.14.11949. Methods are RVAs, NOT file offsets.',
             '// Internal address = ELF load bias + RVA; fields are object-relative.',
             'namespace offsets {']
    for name, (ns, cls, sig) in METHODS.items():
        lines += [f'// {ns}.{cls}: {sig}',
                  f'inline constexpr uintptr_t {name} = 0x{method_rva(block(ns, cls), sig):X};']
    for name, (ns, cls, field) in FIELDS.items():
        lines += [f'// {ns}.{cls}.{field}',
                  f'inline constexpr uintptr_t {name} = 0x{field_offset(block(ns, cls), field):X};']
    return '\n'.join(lines + ['} // namespace offsets', ''])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', nargs='?', type=Path, default=ROOT.parent / 'dump 1.14.11949.zip')
    parser.add_argument('--write', action='store_true', help='replace the checked-in header')
    args = parser.parse_args()
    try:
        expected = render(read_dump(args.dump))
        header = ROOT / 'jni/include/GameOffsets.h'
        if args.write:
            header.write_text(expected)
            print('Wrote', header)
        elif not header.exists() or header.read_text() != expected:
            raise ValueError('GameOffsets.h differs from dump; review, then run --write')
        else:
            print(f'PASS: {len(METHODS)} RVAs and {len(FIELDS)} fields match the supplied dump')
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        sys.exit(f'FAIL: {exc}')

if __name__ == '__main__':
    main()
