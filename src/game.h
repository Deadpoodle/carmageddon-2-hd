#pragma once
// Addresses in the Steam CARMA2_HW.EXE (build 266224). Every patch site and
// global the patch uses is listed here; see re/*.md for how each was found.
#include <cstdint>

namespace game {

// Renderer (re/display.md)
constexpr uintptr_t RendererDeviceName = 0x6621dc;  // const char*: "3DFX_WIN" (0x6622b8) or "D3D" (-d3d)
constexpr uintptr_t NameGlide = 0x6622b8;
constexpr uintptr_t NameD3D = 0x662518;

// Cameras (re/camera-hud.md)
constexpr uintptr_t InitialiseCameras = 0x47e3b0;      // void(void): creates gCamera_list[2], rear-view camera
constexpr uintptr_t CallInitialiseCameras = 0x47df0e;  // its only call, in InitialiseDeathRace 0x47dd20
constexpr uintptr_t CameraList = 0x74c7b0;             // br_actor* gCamera_list[2]; [1] is gCamera, the view
constexpr int ActorTypeData = 0x5c;                    // br_actor -> type_data (br_camera*)
constexpr int CameraAspect = 0x10;                     // br_camera -> aspect (float)

}  // namespace game
