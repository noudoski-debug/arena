#pragma once
#include <cstdint>
// Generated from dump 1.14.11949. Methods are RVAs, NOT file offsets.
// Internal address = ELF load bias + RVA; fields are object-relative.
namespace offsets {
// UnityEngine.Camera: public Vector3 WorldToScreenPoint(Vector3 position)
inline constexpr uintptr_t WorldToScreenPoint = 0xCBA3768;
// UnityEngine.Transform: public Vector3 get_position()
inline constexpr uintptr_t GetPosition = 0xCC39128;
// UnityEngine.Component: public Transform get_transform()
inline constexpr uintptr_t GetTransform = 0xCC1FA20;
// UnityEngine.Camera: public static Camera get_main()
inline constexpr uintptr_t GetMainCamera = 0xCBA4214;
// UnityEngine.Screen: public static int get_width()
inline constexpr uintptr_t GetScreenWidth = 0xCBB746C;
// UnityEngine.Screen: public static int get_height()
inline constexpr uintptr_t GetScreenHeight = 0xCBB7494;
// Mirror.NetworkBehaviour: public bool get_isLocalPlayer()
inline constexpr uintptr_t IsLocalPlayer = 0xC050A88;
// Oxide.PlayerManager: private void Update()
inline constexpr uintptr_t PlayerUpdate = 0x6992B00;
// Oxide.PlayerManager: private void OnDisable()
inline constexpr uintptr_t PlayerOnDisable = 0x69923D4;
// Oxide.PlayerManager.respawning
inline constexpr uintptr_t Respawning = 0x228;
// Oxide.PlayerManager.kicked
inline constexpr uintptr_t Kicked = 0x275;
// Oxide.PlayerManager.userID
inline constexpr uintptr_t UserId = 0x298;
} // namespace offsets
