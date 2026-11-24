#pragma once

#include <d3d9.h>

void Overlay_Render(IDirect3DDevice9* device);
void Overlay_OnReset();
// Draw world-space BO1 machines inside RE4's 3D pass (called from StretchRect
// while the scene depth is still bound, so walls/ground hide them correctly).
void Overlay_DrawWorldModels(IDirect3DDevice9* device);
