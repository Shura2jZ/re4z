#pragma once

#include "common.h"

#include <d3d9.h>

void ViewModel_OnReset();
void ViewModel_Render(IDirect3DDevice9* device, float screenW, float screenH, bool active);
