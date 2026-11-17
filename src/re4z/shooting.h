#pragma once

#include "common.h"

// BO1-owned shooting: the ASI takes the shot, applies the BO1 cadence, and
// routes the damage through RE4's own BulletHit so Ganados react exactly as in
// RE4 (blood, stagger, death). RE4's own Leon shots are neutralised so the
// weapon behaves like BO1 and not like the hidden RE4 gun.
bool Shooting_Init(HMODULE gameModule);

// Called once per camera frame on the game thread with the current FPS eye and
// forward direction (RE4 units, Y up).
void Shooting_OnFrame(const Vec3& eye, const Vec3& dir);
