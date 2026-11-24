#pragma once
#include <d3d9.h>

// Load a static boshjerns-style .r4m model (v2) and draw it inside RE4's 3D
// scene at a fixed world position, using the game's own render camera (pG+0xA4
// view, pG+0xD8 projection). The game's depth buffer hides it behind walls.
void WorldModel_DrawAt(IDirect3DDevice9* device, const wchar_t* file,
                       float wx, float wy, float wz, float yaw, float scale,
                       bool zUp = false);
// Draw with an explicit world matrix (animated parts such as the mystery-box lid
// and the weapon hovering above it). Same camera, depth and state handling.
void WorldModel_DrawMatrix(IDirect3DDevice9* device, const wchar_t* file,
                           const D3DMATRIX& world);
// Center the model on its bounding box, spin it around world Y and place it.
void WorldModel_DrawCentered(IDirect3DDevice9* device, const wchar_t* file,
                             float wx, float wy, float wz, float yaw, float scale,
                             bool zUp);
// Rotate the model about a local X hinge (mystery-box lid opening).
void WorldModel_DrawHinged(IDirect3DDevice9* device, const wchar_t* file,
                           float wx, float wy, float wz, float yaw,
                           float hingeY, float pitch, float scale, bool zUp);
// Place the model at a fixed offset from the stage spawn point (anchored to the
// world, not the camera). The anchor is captured once per room.
void WorldModel_DrawAtSpawn(IDirect3DDevice9* device, const wchar_t* file,
                            float offX, float offY, float offZ, float yaw, float scale,
                            bool zUp = false);
// Draw a textured billboard quad at a world position (for wall-buy weapon
// silhouettes in chalk white). Same camera + depth as the machines.
void WorldModel_DrawBillboard(IDirect3DDevice9* device, IDirect3DTexture9* tex,
                              float wx, float wy, float wz, float yaw, float w, float h);
void WorldModel_Reset();   // release state block on device Reset
