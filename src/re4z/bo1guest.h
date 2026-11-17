#pragma once

#include <d3d9.h>

// BO1 guest compositor: reads the shared-memory frame produced by bo1z.dll and
// draws it over the RE4 view.

void Bo1Guest_OnReset();
bool Bo1Guest_Render(IDirect3DDevice9* device, float viewW, float viewH);
