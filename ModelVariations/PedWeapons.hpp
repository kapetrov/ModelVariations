#pragma once

#include <chrono>

extern char currentZone[9];
extern std::chrono::milliseconds gameplayTimeSinceLoad;
extern char currentMission[9];

class PedWeaponVariations
{
public:
	static void ClearData();
	static void LoadData();
	static void Process();

	//Logging
	static void LogDataFile();

	//Call hooks
	static void InstallHooks();
};
