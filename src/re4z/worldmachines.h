#pragma once
#include <cstdint>

// BO1 machines placed as real world objects in the village.
enum WorldMachineType {
    WM_MYSTERY_BOX = 0,
    WM_PACKAPUNCH  = 1,
    WM_JUGGERNOG   = 2,
    WM_QUICKREVIVE = 3,
    WM_SPEEDCOLA   = 4,
    WM_DOUBLETAP   = 5,
    WM_COUNT       = 6,
};

struct WorldMachine {
    const wchar_t* file;
    float offX, offY, offZ;  // offset from the world anchor (RE4 units, ~mm)
    float yaw, scale;
    bool zUp;
    WorldMachineType type;
};

const WorldMachine* WorldMachines();
WorldMachineType WorldMachine_Type(int index);
// World anchor (fixed). Machines stay at anchor + offset, never camera-follow.
void WorldMachines_Anchor(float out[3]);
// Returns the machine whose interaction radius contains (x,z), or -1.
int WorldMachines_Near(float x, float z, float radius);
// Called by BO1Z when the player interacts (F) near a machine.
void WorldMachines_Interact(int index);
