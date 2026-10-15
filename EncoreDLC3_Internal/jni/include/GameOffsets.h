#pragma once
#include <cstdint>
// Generated from dump 1.14.11949. Methods are RVAs, NOT file offsets.
// Internal address = ELF load bias + RVA; fields are object-relative.
namespace offsets {
// UnityEngine.Physics: public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance, int layerMask, QueryTriggerInteraction queryTriggerInteraction)
inline constexpr uintptr_t PhysicsRaycast = 0xCCDE234;
// UnityEngine.RaycastHit: public Transform get_transform()
inline constexpr uintptr_t HitTransform = 0xCCE76C4;
// UnityEngine.Transform: public bool IsChildOf(Transform parent)
inline constexpr uintptr_t IsChildOf = 0xCC3D400;
// UnityEngine.PhysicsScene: private static bool Internal_Raycast_Injected(in PhysicsScene physicsScene, in Ray ray, float maxDistance, ref RaycastHit hit, int layerMask, QueryTriggerInteraction queryTriggerInteraction)
inline constexpr uintptr_t RaycastInjected = 0xCCE64F0;
// Oxide.PlayerManager: private void LateUpdate()
inline constexpr uintptr_t PlayerLateUpdate = 0x699E0E0;
// Oxide.FPHitscan: private void Update()
inline constexpr uintptr_t WeaponUpdate = 0x68406D8;
// Oxide.FPManager: private void LateUpdate()
inline constexpr uintptr_t FpLateUpdate = 0x6849FAC;
// UnityEngine.Transform: public void set_position(Vector3 value)
inline constexpr uintptr_t SetPosition = 0xCC39204;
// UnityEngine.Transform: public Quaternion get_rotation()
inline constexpr uintptr_t GetRotation = 0xCC39660;
// UnityEngine.Transform: public void set_rotation(Quaternion value)
inline constexpr uintptr_t SetRotation = 0xCC39748;
// UnityEngine.Transform: public Vector3 get_forward()
inline constexpr uintptr_t GetForward = 0xCC39BCC;
// UnityEngine.Transform: public Vector3 get_right()
inline constexpr uintptr_t GetRight = 0xCC3999C;
// UnityEngine.Camera: public float get_fieldOfView()
inline constexpr uintptr_t GetFov = 0xCB9D098;
// UnityEngine.Camera: public void set_fieldOfView(float value)
inline constexpr uintptr_t SetFov = 0xCB9D14C;
// UnityEngine.Camera: public float get_aspect()
inline constexpr uintptr_t GetAspect = 0xCB9E4BC;
// UnityEngine.Camera: public void set_aspect(float value)
inline constexpr uintptr_t SetAspect = 0xCB9E570;
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
// Oxide.PlayerManager.vitals
inline constexpr uintptr_t Vitals = 0xC8;
// Oxide.GenericVitals.m_MaxHealth
inline constexpr uintptr_t MaxHealth = 0x88;
// Oxide.PlayerManager.playerFlags
inline constexpr uintptr_t PlayerFlags = 0x270;
// Oxide.PlayerManager.playerEventHandler
inline constexpr uintptr_t PlayerEvent = 0x78;
// .Vsj.Health
inline constexpr uintptr_t EntityHealth = 0x98;
// .Vsj.DisplayName
inline constexpr uintptr_t EntityName = 0x88;
// Oxide.PlayerManager.worldCameraRoot
inline constexpr uintptr_t WorldCameraRoot = 0x68;
// Oxide.PlayerManager.mouseLook
inline constexpr uintptr_t MouseLook = 0x70;
// Oxide.PlayerManager.lookAngle
inline constexpr uintptr_t LookAngle = 0x1C8;
// Oxide.PlayerManager.lastTickPosition
inline constexpr uintptr_t LastTickPosition = 0x1E8;
// Oxide.PlayerManager.fpManager
inline constexpr uintptr_t FpManager = 0x90;
// Oxide.FPManager.m_WorldCamera
inline constexpr uintptr_t WorldCamera = 0x20;
// Oxide.FPManager._currentWeapon
inline constexpr uintptr_t CurrentWeapon = 0x50;
// Oxide.FPManager.<qkr>k__BackingField
inline constexpr uintptr_t ScopeBlend = 0xA8;
// Oxide.MouseLook.qxz
inline constexpr uintptr_t MouseAngles = 0x4C;
// Oxide.FPObject.Player
inline constexpr uintptr_t WeaponPlayer = 0xC0;
// Oxide.FPObject.normalFOV
inline constexpr uintptr_t NormalFov = 0x80;
// Oxide.FPObject.aimFOV
inline constexpr uintptr_t AimFov = 0x84;
// Oxide.FPHitscan.config
inline constexpr uintptr_t WeaponConfig = 0x130;
// HyperHug.Games.Oxide.Features.Weapons.HitscanWeaponConfig.recoilConfig
inline constexpr uintptr_t RecoilConfig = 0x70;
// HyperHug.Games.Oxide.Features.Weapons.HitscanWeaponConfig.dispersionConfig
inline constexpr uintptr_t DispersionConfig = 0x68;
// HyperHug.Games.Oxide.Features.Weapons.RecoilConfig.<MinRecoil>k__BackingField
inline constexpr uintptr_t MinRecoil = 0x18;
// HyperHug.Games.Oxide.Features.Weapons.RecoilConfig.<MaxRecoil>k__BackingField
inline constexpr uintptr_t MaxRecoil = 0x20;
// HyperHug.Games.Oxide.Features.Weapons.DispersionConfig.minFireDispersion
inline constexpr uintptr_t MinFireDispersion = 0x18;
// HyperHug.Games.Oxide.Features.Weapons.DispersionConfig.maxFireDispersion
inline constexpr uintptr_t MaxFireDispersion = 0x1C;
// HyperHug.Games.Oxide.Features.Weapons.DispersionConfig.minDispersion
inline constexpr uintptr_t MinDispersion = 0x20;
// HyperHug.Games.Oxide.Features.Weapons.DispersionConfig.maxDispersion
inline constexpr uintptr_t MaxDispersion = 0x24;
// HyperHug.Games.Oxide.Features.Weapons.DispersionConfig.dispersionIncreasePerShot
inline constexpr uintptr_t DispersionIncrease = 0x28;
// Oxide.PlayerManager.respawning
inline constexpr uintptr_t Respawning = 0x228;
// Oxide.PlayerManager.kicked
inline constexpr uintptr_t Kicked = 0x275;
// Oxide.PlayerManager.userID
inline constexpr uintptr_t UserId = 0x298;
} // namespace offsets
