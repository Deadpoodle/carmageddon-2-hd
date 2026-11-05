// Traffic is drawn only inside a box around the camera, so a car used to pop into
// view at the box's edge, and at twice the original distance that edge is in plain
// sight. Now a car's opacity follows how far inside the box it is: nothing to time,
// and it works the same way in and out. The fade is kept narrow: at 8 units a third
// of the cars on screen were see-through at any moment, next to solid ones.
//
// Opacity belongs to a material, and all the cars of a type share theirs, so each
// level is a copy of the car: models with the same vertices and faces, whose faces
// use copies of the materials with an opacity in extra_prim (the tokens the front
// end fades its panels with, 0x46cf6f). Choosing a level is a pointer or two in each
// actor: wheels and some body faces have no material of their own and use the actor's.
// A see-through car goes through the primitive heap (actors.cpp).
//
// A car that has been hit is left solid. It takes a model from a pool for its type
// that shares the faces and has vertices of its own to dent, which has no copies, so
// its body wouldn't fade while its wheels did. Resetting a car puts the body back on
// its first model (0x44cf33) whatever level it was at, which is why the level is
// set every frame and not only when it changes.
//
// The copies are built after InitDrones 0x44f700 and freed when the game frees
// its drones, 0x44fc10.
#include "trafficfade.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "hook.h"
#include "log.h"

namespace trafficfade {
namespace {

// BRender, all cdecl.
const auto kModelAllocate = (uint8_t * (*)(const char*, int, int))0x5217f0;
const auto kModelAdd = (void* (*)(uint8_t*))0x51ed00;
const auto kModelRemove = (void* (*)(uint8_t*))0x51ed30;
const auto kModelFree = (void (*)(uint8_t*))0x5217e0;
const auto kMaterialAllocate = (uint8_t * (*)(const char*))0x51f240;
const auto kMaterialAdd = (void* (*)(uint8_t*))0x51ee50;
const auto kMaterialRemove = (void* (*)(uint8_t*))0x51ee80;
const auto kMaterialFree = (void (*)(uint8_t*))0x51f2a0;

const auto kInitDrones = (void(__fastcall*)(int))0x44f700;
const auto kProcessDrones = (void (*)())0x4512f0;
void (*o_DisposeDrones)();

uint8_t* const* const kDrones = (uint8_t* const*)0x684504;
const int* const kDroneCount = (const int*)0x6820d0;
const float* const kBoxCentre = (const float*)0x6820c0;
constexpr int kDroneSize = 0x5d8, kDroneType = 4, kDroneState = 0x38, kDroneActor = 0xec;
constexpr int kKnockedAbout = 3, kStopped = 4;
constexpr int kTypeKind = 0x10;  // 0 is a road car
constexpr int kActorNext = 0, kActorChildren = 8, kActorModel = 0x18, kActorMaterial = 0x1c, kActorStyle = 0x20;
constexpr int kActorPosition = 0x50;
constexpr uint8_t kStyleDrawn = 4;
constexpr int kVertexSize = 40, kFaceSize = 40, kFaceMaterial = 8;
constexpr int kModelName = 4, kModelVertices = 8, kModelFaces = 0xc, kModelVertexCount = 0x10, kModelFaceCount = 0x12;
constexpr int kModelPivot = 0x14, kModelFlags = 0x20;
constexpr int kMaterialName = 4, kMaterialSize = 0x9c, kMaterialStored = 0x98, kMaterialExtraPrim = 0x58;

constexpr int kLevels = 5;  // see-through levels, evenly spaced; a car past them is the original
constexpr float kFadeWidth = 4;

uint32_t g_tokens[kLevels][6];  // BLEND_B = 1, OPACITY_X = opacity, end
float g_across, g_up;

struct Copies {
    uint8_t* original;
    uint8_t* faded[kLevels];
};
std::vector<Copies> g_models, g_materials;
bool g_built;

uint8_t*& Field(uint8_t* p, int offset) { return *(uint8_t**)(p + offset); }

Copies* Find(std::vector<Copies>& list, uint8_t* p) {
    for (Copies& c : list) {
        if (c.original == p) return &c;
        for (uint8_t* f : c.faded)
            if (f == p) return &c;
    }
    return nullptr;
}

uint8_t* FadedMaterial(uint8_t* material, int level) {
    Copies* found = Find(g_materials, material);
    if (found) return found->faded[level];
    Copies c = {material, {}};
    const char* name = (const char*)Field(material, kMaterialName);
    for (int i = 0; i < kLevels; i++) {
        char id[64];
        snprintf(id, sizeof id, "%.48s~fade%d", name ? name : "", i);
        uint8_t* copy = kMaterialAllocate(id);
        memcpy(copy + 8, material + 8, kMaterialStored - 8);
        Field(copy, kMaterialExtraPrim) = (uint8_t*)g_tokens[i];
        kMaterialAdd(copy);
        c.faded[i] = copy;
    }
    g_materials.push_back(c);
    return c.faded[level];
}

void CopyModel(uint8_t* model) {
    if (Find(g_models, model)) return;
    int vertices = *(uint16_t*)(model + kModelVertexCount), faces = *(uint16_t*)(model + kModelFaceCount);
    if (!vertices || !faces || !Field(model, kModelVertices) || !Field(model, kModelFaces)) return;
    Copies c = {model, {}};
    const char* name = (const char*)Field(model, kModelName);
    for (int i = 0; i < kLevels; i++) {
        char id[64];
        snprintf(id, sizeof id, "%.48s~fade%d", name ? name : "", i);
        uint8_t* copy = kModelAllocate(id, vertices, faces);
        memcpy(Field(copy, kModelVertices), Field(model, kModelVertices), (size_t)vertices * kVertexSize);
        memcpy(Field(copy, kModelFaces), Field(model, kModelFaces), (size_t)faces * kFaceSize);
        memcpy(copy + kModelPivot, model + kModelPivot, kModelFlags - kModelPivot);
        *(uint16_t*)(copy + kModelFlags) = *(uint16_t*)(model + kModelFlags) & 0x7fff;
        for (int f = 0; f < faces; f++) {
            uint8_t*& material = Field(Field(copy, kModelFaces) + f * kFaceSize, kFaceMaterial);
            if (material) material = FadedMaterial(material, i);
        }
        kModelAdd(copy);
        c.faded[i] = copy;
    }
    g_models.push_back(c);
}

// The actors under a car. The car's own top actor has no model, and its siblings
// are everything else in the world.
template <typename F>
void ForEachPart(uint8_t* actor, F f) {
    for (uint8_t* child = Field(actor, kActorChildren); child; child = Field(child, kActorNext)) {
        f(child);
        ForEachPart(child, f);
    }
}

bool IsRoadCar(uint8_t* drone) { return *(int*)(Field(drone, kDroneType) + kTypeKind) == 0; }

void Choose(std::vector<Copies>& list, uint8_t*& used, int level) {
    if (!used) return;
    if (Copies* c = Find(list, used)) used = level < kLevels ? c->faded[level] : c->original;
}

// The level on a whole car, or the original with a level of kLevels.
void SetLevel(uint8_t* drone, int level) {
    ForEachPart(Field(drone, kDroneActor), [level](uint8_t* actor) {
        Choose(g_models, Field(actor, kActorModel), level);
        Choose(g_materials, Field(actor, kActorMaterial), level);
    });
}

// How far the car is inside the box, by its nearest face.
float Margin(const float* at) {
    float x = g_across - fabsf(at[0] - kBoxCentre[0]), y = g_up - fabsf(at[1] - kBoxCentre[1]),
          z = g_across - fabsf(at[2] - kBoxCentre[2]);
    return x < y ? (x < z ? x : z) : (y < z ? y : z);
}

void Build() {
    if (g_built) return;
    g_built = true;
    for (int i = 0; i < *kDroneCount; i++) {
        uint8_t* drone = *kDrones + i * kDroneSize;
        if (!IsRoadCar(drone)) continue;
        ForEachPart(Field(drone, kDroneActor), [](uint8_t* actor) {
            if (uint8_t* model = Field(actor, kActorModel)) CopyModel(model);
            if (uint8_t* material = Field(actor, kActorMaterial)) FadedMaterial(material, 0);
        });
    }
    Log("trafficfade: %u models and %u materials copied", (unsigned)g_models.size(), (unsigned)g_materials.size());
}

void Free() {
    if (!g_built) return;
    for (int i = 0; i < *kDroneCount; i++) {
        uint8_t* drone = *kDrones + i * kDroneSize;
        if (IsRoadCar(drone)) SetLevel(drone, kLevels);
    }
    for (Copies& c : g_models)
        for (uint8_t* copy : c.faded) {
            kModelRemove(copy);
            kModelFree(copy);
        }
    for (Copies& c : g_materials)
        for (uint8_t* copy : c.faded) {
            kMaterialRemove(copy);
            kMaterialFree(copy);
        }
    Log("trafficfade: %u models and %u materials freed", (unsigned)g_models.size(), (unsigned)g_materials.size());
    g_models.clear();
    g_materials.clear();
    g_built = false;
}

void __fastcall InitDrones(int race) {
    kInitDrones(race);
    Build();
}

void DisposeDrones() {
    Free();
    o_DisposeDrones();
}

void ProcessDrones() {
    kProcessDrones();
    if (!g_built) return;
    for (int i = 0; i < *kDroneCount; i++) {
        uint8_t* drone = *kDrones + i * kDroneSize;
        uint8_t* actor = Field(drone, kDroneActor);
        if (!IsRoadCar(drone) || *(actor + kActorStyle) != kStyleDrawn) continue;
        float margin = Margin((const float*)(actor + kActorPosition));
        int level = margin >= kFadeWidth ? kLevels : margin <= 0 ? 0 : (int)(margin / kFadeWidth * kLevels);
        int state = *(int*)(drone + kDroneState);
        SetLevel(drone, state == kKnockedAbout || state == kStopped ? kLevels : level);
    }
}

}  // namespace

void Install(float across, float up) {
    char stock[4] = "";
    if (GetEnvironmentVariableA("C2HD_STOCK_FADE", stock, sizeof stock) && stock[0] == '1') {
        Log("trafficfade: traffic pops in as the original (C2HD_STOCK_FADE)");
        return;
    }
    g_across = across, g_up = up;
    for (int i = 0; i < kLevels; i++) {
        uint32_t opacity = 255 * (i + 1) / (kLevels + 1);
        uint32_t list[6] = {0x85, 1, 0xbe, opacity << 16, 0, 0};
        memcpy(g_tokens[i], list, sizeof list);
    }
    static const uint8_t disposeStart[5] = {0xa1, 0xd0, 0x20, 0x68, 0x00};  // mov eax, [0x6820d0]
    o_DisposeDrones = (void (*)())hook::Detour(0x44fc10, disposeStart, 5, (const void*)&DisposeDrones);
    bool ok = o_DisposeDrones && hook::Redirect(0x401144, 0xe8, 0x44f700, (const void*)&InitDrones) &&
              hook::Redirect(0x40123b, 0xe8, 0x4512f0, (const void*)&ProcessDrones);
    Log("trafficfade: traffic fades at the edge of its box: %s", ok ? "on" : "FAILED");
}

}  // namespace trafficfade
