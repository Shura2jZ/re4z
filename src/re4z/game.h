#pragma once

#include "common.h"

bool GameInit(HMODULE gameModule);

uintptr_t GameBase();
bool IsRangeReadable(const void* ptr, size_t size);
void* GetGame();
void* GetPlayer();
bool ReadPlayerPos(Vec3* out);
void SetPlayerVisible(bool visible);
void SetPlayerWeaponsVisible(bool visible);
void SetPlayerPartsVisible(bool visible);
bool IsPlayerAlive();

uintptr_t GetPLGlobal();
uintptr_t GetPGGlobal();
