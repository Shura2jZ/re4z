#pragma once

#include "common.h"

// Read-only Zombies HUD state. Everything here is derived from the running RE4
// session (global game work + EmMgr); no game memory is modified.
struct ZombiesHud {
    bool inGame = false;
    bool valid = false;
    int round = 1;
    int alive = 0;
    int kills = 0;
    int points = 0;
    int health = 0;
    int maxHealth = 0;
    int roomId = 0;
    int weaponNo = 0;
    int weaponId = 0;
    int ammoLoaded = 0;
    int ammoReserve = 0;
    bool ammoValid = false;
    float healthFrac = 0.0f;
    bool hitmarker = false;
    bool hitmarkerKill = false;
    bool roundBanner = false;
    bool roundComplete = false;
    int pointsPopup = 0;
    int maxAlive = 6;       // BO1-style max alive zombies for this round
    int spawnIntervalMs = 2000;  // BO1-style spawn interval
    // BO1 round bookkeeping (Ascension gametype).
    int roundSize = 0;      // zombies required to finish the round
    int roundKills = 0;     // kills already scored in this round
    bool roundIntermission = false;  // between rounds (BO1 intermission)
    float roundPulse = 0.0f; // 0..1 chalk pulse of the corner round number
    // Score popup animation (BO1 score_highlight, _zombiemode_score.gsc:541).
    float popupAge = 0.0f;    // seconds since the popup appeared
    float popupDx = 0.0f;    // horizontal drift in 704-space pixels
    float popupDy = 0.0f;    // vertical drift in 704-space pixels
    // BO1 map intro: 1 = full black veil, fading to 0 during the fade-in.
    float introBlack = 0.0f;
    bool introActive = false;
    // BO1 perks: owned flags, 0..1 pickup pop per badge, buy menu open.
    bool perkOwned[4] = {};
    float perkPop[4] = {};
    bool perkMenu = false;
};

void Zombies_Update();
const ZombiesHud& Zombies_Get();
const char* Zombies_WeaponName(int itemId);
void Zombies_PerkInfo(int slot, const char** name, int* cost, bool* owned);

// BO1 Zombies round table (Ascension gametype formulas, see zombies.cpp).
int Zombies_RoundSize(int round);        // zombies to kill during the round
int Zombies_RoundStartKills(int round);  // cumulative kills at the round start
int Zombies_ZombieHealth(int round);     // zombie health for the round
float Zombies_SpawnDelay(int round);     // seconds between two spawns
float Zombies_MoveSpeed(int round);      // zombie move speed
int Zombies_MaxAlive(int round);         // concurrent enemies cap
int Zombies_SpawnInterval(int round);    // spawn interval in ms
