#pragma once

#include <CTheZones.h>

#include <string>
#include <unordered_map>
#include <vector>

extern std::unordered_map<unsigned short, std::string> modelNames;
extern std::unordered_map<std::string, std::vector<CZone*>> presetAllZones;
extern bool forceEnableGlobal;
extern char currentZone[9];

enum class debugDrawVehStats
{
	POINTER = 1,
	MODEL = 2,
	HEALTH = 4,
	PARENT_MODEL = 8,
};

struct vehTimeGroup {
	unsigned short start = 0;
	unsigned short end = 0;
	std::vector<unsigned short> occupantGroups;
	std::vector<unsigned short> trailers;
	std::vector<unsigned short> variations;
};

class VehicleVariations
{
public:
	static void ClearData();
	static void LoadData();
	static void Process();
	static void UpdateVariations();
	static void DrawDebugInfo(float fontSize, uint32_t debugOptions);

	//Logging
	static void LogCurrentVariations();
	static void LogDataFile();
	static void LogVariations();

	//Call hooks
	static void InstallHooks();
};
