#pragma once

#include "common.h"

bool InstallCameraHook(HMODULE gameModule);
bool Camera_IsEnabled();
// Latest camera point (world space) for projecting world objects in the overlay.
const CameraPoint& Camera_GetLatest();
