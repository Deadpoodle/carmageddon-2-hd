#pragma once
// Addresses in the Steam CARMA2_HW.EXE (build 266224). Every patch site and
// global the patch uses is listed here; see re/*.md for how each was found.
#include <cstdint>

namespace game {

// Renderer (re/display.md)
constexpr uintptr_t RendererDeviceName = 0x6621dc;  // const char*: "3DFX_WIN" (0x6622b8) or "D3D" (-d3d)
constexpr uintptr_t NameGlide = 0x6622b8;
constexpr uintptr_t NameD3D = 0x662518;

// Screen size (re/display.md, re/camera-hud.md plan B)
constexpr uintptr_t DeviceWidthImm = 0x51c34f;   // push 0x280 (WIDTH_I32) in PDAllocateScreenAndBack 0x51c300
constexpr uintptr_t DeviceHeightImm = 0x51c348;  // push 0x1e0 (HEIGHT_I32)
constexpr uintptr_t GrafSpec640 = 0x66223c;      // gGraf_specs[1]: +0xc total w, +0x10 total h, +0x24 row, +0x28 phys w, +0x2c phys h

// D3D.BDD (re/d3d-driver.md): RVAs in the driver image, which BRender maps at
// an address of its own. Found by the mode picker's code signature.
constexpr uint32_t DrvSigRva = 0x25a8;          // eb 6f 8b 7e 0c b8 80 02 00 00 3b f8 b9 e0 01 00 00
constexpr uint32_t DrvFormat512W = 0x1709c;     // screen-format table row 512x384 (reused for the wide size)
constexpr uint32_t DrvFormat512H = 0x170a0;
constexpr uint32_t DrvPickerWidth = 0x25ae;     // mode picker looks for 640 (mov eax, 0x280)
constexpr uint32_t DrvPickerHeight = 0x25b5;    // ... x 480 (mov ecx, 0x1e0)
constexpr uint32_t DrvModeWidth = 0x15888;      // default mode width (viewport, depth buffer)
constexpr uint32_t DrvModeHeight = 0x1588c;
constexpr uint32_t DrvClipX = 0x196b;           // single-pixel clip: cmp eax, 0x27f
constexpr uint32_t DrvNo2DBufferSet = 0x220f;   // mov [0x1001bb9c], ebx: enables the static 640x480 2D buffer

// Cameras (re/camera-hud.md)
constexpr uintptr_t InitialiseCameras = 0x47e3b0;      // void(void): creates gCamera_list[2], rear-view camera
constexpr uintptr_t CallInitialiseCameras = 0x47df0e;  // its only call, in InitialiseDeathRace 0x47dd20
constexpr uintptr_t CameraList = 0x74c7b0;             // br_actor* gCamera_list[2]; [1] is gCamera, the view
constexpr int ActorTypeData = 0x5c;                    // br_actor -> type_data (br_camera*)
constexpr int CameraAspect = 0x10;                     // br_camera -> aspect (float)

}  // namespace game
