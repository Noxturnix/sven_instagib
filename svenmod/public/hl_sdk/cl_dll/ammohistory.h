/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// ammohistory.h
//

#ifndef SDK_AMMOHISTORY_H
#define SDK_AMMOHISTORY_H

#ifdef _WIN32
#pragma once
#endif

#include "ammo.h"

class WeaponsResource
{
private:
	WEAPON *rgSlots[MAX_WEAPON_SLOTS + 1][MAX_WEAPON_POSITIONS + 1];	// The slots currently in use by weapons.  The value is a pointer to the weapon;  if it's NULL, no weapon is there
	int	riAmmo[MAX_AMMO_TYPES];							// count of each ammo type

public:

	WEAPON *GetWeaponSlot(int slot, int pos)
	{
		return rgSlots[slot][pos];
	}

	void LoadWeaponSprites(WEAPON *wp);
	void LoadAllWeaponSprites();
	WEAPON *GetFirstPos(int iSlot);
	void SelectSlot(int iSlot, int fAdvance, int iDirection);
	WEAPON *GetNextActivePos(int iSlot, int iSlotPos);

	int HasAmmo(WEAPON *p);

	///// AMMO /////
	AMMO GetAmmo(int iId)
	{
		return iId;
	}

	void SetAmmo(int iId, int iCount)
	{
		riAmmo[iId] = iCount;
	}

	int CountAmmo(int iId);

	VHSPRITE *GetAmmoPicFromWeapon(int iAmmoId, Rect &rect);
};

extern WeaponsResource gWR;
extern WeaponsResource *g_pWeaponsResource;

// ToDo: reverse HistoryResource, never touched it

#define MAX_HISTORY 12
enum
{
	HISTSLOT_EMPTY,
	HISTSLOT_AMMO,
	HISTSLOT_WEAP,
	HISTSLOT_ITEM,
};

class HistoryResource
{
private:
	struct HIST_ITEM
	{
		int type;
		float DisplayTime; // the time at which this item should be removed from the history
		int iCount;
		int iId;
	};

	HIST_ITEM rgAmmoHistory[MAX_HISTORY];

public:
	void Init()
	{
		Reset();
	}

	void Reset()
	{
		memset(rgAmmoHistory, 0, sizeof rgAmmoHistory);
	}

	int iHistoryGap;
	int iCurrentHistorySlot;

	void AddToHistory(int iType, int iId, int iCount = 0);
	void AddToHistory(int iType, const char* szName, int iCount = 0);

	void CheckClearHistory();
	bool DrawAmmoHistory(float flTime);
};

extern HistoryResource gHR;

#endif // SDK_AMMOHISTORY_H