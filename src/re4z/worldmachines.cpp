// worldmachines.cpp: the 6 BO1 machines placed as fixed world objects, plus
// proximity detection and interaction hooks. Positions are anchor + offset;
// the anchor is fixed in the village, so nothing follows the camera.
#include "worldmachines.h"
#include "log.h"

#include <cmath>
#include <cstdio>

namespace {

const WorldMachine kMachines[WM_COUNT] = {
    { L"mysterybox.r4m",       0.0f,     0.0f, -3000.0f, 0.0f,  9.0f, true,  WM_MYSTERY_BOX },
    { L"packapunch.r4m",    3000.0f,     0.0f, -3000.0f, 1.57f, 8.0f, true,  WM_PACKAPUNCH  },
    { L"machine_jugg.r4m", -3000.0f,     0.0f, -3000.0f, 0.0f,  8.0f, true,  WM_JUGGERNOG   },
    { L"machine_revive.r4m",-3000.0f,    0.0f,  0.0f,    0.0f,  8.0f, true,  WM_QUICKREVIVE },
    { L"machine_sleight.r4m", 3000.0f,   0.0f,  0.0f,    0.0f,  8.0f, true,  WM_SPEEDCOLA   },
    { L"machine_doubletap.r4m",0.0f,     0.0f, 3000.0f,  3.14f, 8.0f, true,  WM_DOUBLETAP   },
};

// Fixed village anchor (RE4 units, ~mm). Never derived from the camera.
const float kAnchor[3] = { -40500.0f, 0.0f, -3000.0f };

} // namespace

const WorldMachine* WorldMachines() { return kMachines; }

WorldMachineType WorldMachine_Type(int index) {
    if (index < 0 || index >= WM_COUNT) return WM_MYSTERY_BOX;
    return kMachines[index].type;
}

void WorldMachines_Anchor(float out[3]) {
    out[0] = kAnchor[0]; out[1] = kAnchor[1]; out[2] = kAnchor[2];
}

int WorldMachines_Near(float x, float z, float radius) {
    const float r2 = radius * radius;
    for (int i = 0; i < WM_COUNT; ++i) {
        const float mx = kAnchor[0] + kMachines[i].offX;
        const float mz = kAnchor[2] + kMachines[i].offZ;
        const float dx = x - mx, dz = z - mz;
        if (dx*dx + dz*dz <= r2) return i;
    }
    return -1;
}

void WorldMachines_Interact(int index) {
    if (index < 0 || index >= WM_COUNT) return;
    Log("world machine: interact with type %d at (%.0f %.0f)",
        (int)kMachines[index].type,
        kAnchor[0] + kMachines[index].offX,
        kAnchor[2] + kMachines[index].offZ);
    // Actual effects (buy perk / mystery box / PaP) are wired in zombies.cpp;
    // this is the hook point. Filled in by the interaction system.
}
