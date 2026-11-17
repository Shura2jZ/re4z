#pragma once

#include <d3d9.h>

// Load a DDS (DXT5 or A8R8G8B8) file into a D3D9 texture. Returns nullptr on failure.
IDirect3DTexture9* LoadDdsTexture(IDirect3DDevice9* device, const char* path);

// Build the path to a HUD asset extracted from the BO1 fastfiles.
// Assets are expected under <exeDir>\re4z\assets\<relativePath>
bool BuildAssetPath(char* out, size_t outSize, const char* relativePath);