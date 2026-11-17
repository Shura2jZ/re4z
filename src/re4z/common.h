#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdarg>

struct Vec3 {
    float x, y, z;
};

// CAMERA_POINT from RE4: 0x20 bytes, world space.
struct CameraPoint {
    Vec3 Campos;
    Vec3 Target;
    float Roll;
    float Fovy;
};

static_assert(sizeof(Vec3) == 12, "Vec3 must stay 12 bytes");
static_assert(sizeof(CameraPoint) == 32, "CameraPoint must stay 32 bytes");
