# Oxide Survival Island — dump 1.14.11949 (arm64-v8a)

This manifest is derived from the supplied `dump.cs`. Field offsets below are object-relative offsets from the dump, not ELF file offsets. The `PlayerManager` exact-resolver table in `jni/src/Esp.cpp` uses the **Offset** values (not RVA) listed here.

## PlayerManager (TypeDefIndex 9722)

| Member | Offset |
|---|---:|
| `worldCameraRoot` | `0x68` |
| `mouseLook` | `0x70` |
| `fpManager` | `0x90` |
| `vitals` | `0xC8` |
| `team` | `0x120` |
| `nicklabel` | `0x130` |
| `lookAngle` | `0x1C8` |
| `lastTickPosition` | `0x1E8` |
| `lastSavedPosition` | `0x1F4` |
| `lastDeathPosition` | `0x200` |
| `playerFlags` | `0x270` |
| `kicked` | `0x275` |
| `userID` | `0x298` |

## MouseLook (TypeDefIndex 9334)

| Member | Offset |
|---|---:|
| `m_LookRoot` | `0x28` |
| `m_Sensitivity` | `0x34` |
| `qxz` (Vector2) | `0x4C` |
| `qxq` (Vector2) | `0x70` |
| `<qxM>k__BackingField` (Vector2) | `0x88` |

## FPManager (TypeDefIndex 9417)

| Member | Offset |
|---|---:|
| `m_WorldCamera` | `0x20` |
| `_currentWeapon` | `0x50` |
| `qkc` | `0x9C` |
| `qkn` | `0xA0` |
| `<qkH>k__BackingField` | `0xA4` |
| `<qkr>k__BackingField` | `0xA8` |
| `<qkO>k__BackingField` | `0xAC` |
| `<qkb>k__BackingField` | `0xB0` |
| `<qkS>k__BackingField` | `0xB1` |

## FPObject (TypeDefIndex 9424)

| Member | Offset |
|---|---:|
| `normalFOV` | `0x80` |
| `aimFOV` | `0x84` |

## PlayerManager method file offsets

| Method | `Offset` |
|---|---:|
| `.cctor` | `0x69A2114` |
| `Update` | `0x698EB00` |
| `OnStopClient` | `0x69AC9B0` |
| `DeserializeSyncVars` | `0x6985C10` |
| `OnStartClient` | `0x6998994` |
| `OnStartLocalPlayer` | `0x698667C` |

**Important:** this updates the exact-method resolver and documents dump-derived layouts. It does not prove that every runtime field's semantic meaning survived obfuscation; features relying on runtime values still require testing in the exact game build/device.
