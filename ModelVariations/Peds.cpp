#include "Peds.hpp"
#include "DataReader.hpp"
#include "Helpers.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "Memory.hpp"
#include "SA.hpp"

#include <plugin.h>
#include <ePedType.h>
#include <CFont.h>
#include <CModelInfo.h>
#include <CPed.h>
#include <CPopCycle.h>
#include <CSprite.h>
#include <CTaskComplexCopInCar.h>
#include <CTheZones.h>
#include <CWeather.h>
#include <CWorld.h>

#include <array>
#include <map>
#include <chrono>

static const char* dataFileName = "ModelVariations_Peds.ini";
static DataReader dataFile;
std::vector<int16_t> destroyedModelCounters;

struct tPedVars {
    std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>> variations;
    std::unordered_map<unsigned short, std::array<std::vector<unsigned short>, 6>> wantedVariations;
    std::unordered_map<unsigned short, std::unordered_map<std::string, std::vector<unsigned short>>> missionVariations;
    std::map<unsigned short, std::vector<unsigned short>> currentVariations;
    std::unordered_map<unsigned short, std::vector<pedTimeGroup>> timeGroups;
    std::unordered_map<unsigned short, std::set<unsigned short>> activeTimeGroups;


    std::unordered_map<unsigned short, unsigned short> originalModels;
    std::unordered_map<unsigned short, bool> useParentVoice;
    std::unordered_map<unsigned short, std::vector<unsigned short>> voices;
    std::unordered_map<unsigned short, unsigned int> animGroups;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherSunny;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherRainy;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherFoggy;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherSandstorm;
    std::unordered_map<unsigned short, std::vector<unsigned short>> weatherWindy;

    std::set<unsigned short> pedHasVariations;

    std::vector<CPed*> stack;

    std::vector<unsigned short> disableOnMission;
    std::vector<unsigned short> dontInheritBehaviourModels;
    std::vector<unsigned short> mergeInteriors;
};

static tPedVars pedVars;


struct tPedOptions {
    bool useParentVoices = false;
    bool improveCivilianVariety = false;
};

static tPedOptions pedOptions;

unsigned short variationModel = 0;

bool ignoreCivilianVariety = false;

std::map<CPed*, unsigned short> changedVoices;

struct
{
    bool isRainy = false;
    bool isSandstorm = false;
    bool isFoggy = false;
    bool isWindy = false;
    bool isSunny = false;
} weatherState;

bool isValidPedId(int id)
{
    if (id < 1)
        return false;
    if (id >= 190 && id <= 195)
        return false;

    return true;
}

unsigned short PedVariations::GetVariationOriginalModel(const int modelIndex)
{
    auto it = pedVars.originalModels.find((unsigned short)modelIndex);
    if (it != pedVars.originalModels.end())
        return it->second;

    return { (unsigned short)modelIndex };
}

bool isPedVisible(CPed* ped) 
{
    if (ped == NULL)
        return false;

    CVector camPos = *reinterpret_cast<CVector*>(0xB6F930);
    CVector targetPos = ped->GetPosition();

    CColPoint hitPoint;
    CEntity* hitEntity = nullptr;

    bool hitSomething = CWorld::ProcessLineOfSight(
        camPos,
        targetPos,
        hitPoint,
        hitEntity,
        true,   // buildings
        false,   // vehicles
        false,  // peds
        true,   // objects
        true,   // dummies
        true,   // doSeeThroughCheck
        true,   // doCameraIgnoreCheck
        false   // doShootThroughCheck
    );

    return !hitSomething || hitEntity == ped;
}

bool canPedDriveVeh(int pedModel, int vehModel)
{
    CPedModelInfo* pedInfo = reinterpret_cast<CPedModelInfo*>(CModelInfo::GetModelInfo(pedModel));
    CVehicleModelInfo *vehInfo = reinterpret_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(vehModel));
    if (pedInfo && vehInfo)
        return (pedInfo->m_nCarsCanDriveMask & (1U << vehInfo->m_nVehicleClass));

    return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PedVariations::ClearData()
{
    pedVars.variations.clear();
    pedVars.wantedVariations.clear();
    pedVars.missionVariations.clear();
    pedVars.currentVariations.clear();
    pedVars.timeGroups.clear();
    pedVars.activeTimeGroups.clear();


    pedVars.originalModels.clear();
    pedVars.useParentVoice.clear();
    pedVars.voices.clear();
    pedVars.animGroups.clear();
    pedVars.weatherSunny.clear();
    pedVars.weatherRainy.clear();
    pedVars.weatherFoggy.clear();
    pedVars.weatherSandstorm.clear();
    pedVars.weatherWindy.clear();

    pedVars.pedHasVariations.clear();

    pedVars.stack.clear();

    pedVars.disableOnMission.clear();
    pedVars.dontInheritBehaviourModels.clear();
    pedVars.mergeInteriors.clear();

    pedOptions = {};

    dataFile.Clear();
}

void PedVariations::LoadData()
{
    dataFile.Load(dataFileName);

    Log::Write("\nReading ped data...\n");

    for (auto& iniData : dataFile.data)
    {
        int i = 0;
        std::string section(iniData.first);
        Log::Write("%s\n", section.c_str());

        if (!section.empty() && section[0] >= '0' && section[0] <= '9')
            fromString<int>(iniData.first, i);
        else
            CModelInfo::GetModelInfo(section.data(), &i);

        if (i <= 0 || i > 65535)
            continue;

        unsigned short modelIndex = static_cast<unsigned short>(i);
        if (isValidPedId(modelIndex))
        {
            for (auto& kvp : iniData.second)
            {
                if (auto it = presetAllZones.find(std::string(kvp.first)); it != presetAllZones.end())
                {
                    auto vec = dataFile.ReadLine(section, kvp.first, READ_PEDS);

                    if (!vec.empty())
                    {
                        pedVars.pedHasVariations.insert(modelIndex);
                        if (it->second.empty()) //Global
                        {
                            for (int k = 0; k < CTheZones::TotalNumberOfInfoZones; k++)
                            {
                                CZone* zone = reinterpret_cast<CZone*>(CTheZones__NavigationZoneArray + k * 0x20);
                                uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                                pedVars.variations[zoneName][modelIndex] = vectorUnion(pedVars.variations[zoneName][modelIndex], vec);
                            }
                        }
                        else for (auto zone : it->second)
                        {
                            uint64_t zoneName = *reinterpret_cast<uint64_t*>(zone->m_szLabel);
                            pedVars.variations[zoneName][modelIndex] = vectorUnion(pedVars.variations[zoneName][modelIndex], vec);
                        }
                    }
                }
                else if (kvp.first.size() >= 8 && kvp.first.starts_with("MISSION"))
                {
                    auto vec = dataFile.ReadLine(section, kvp.first, READ_PEDS);

                    if (!vec.empty())
                        pedVars.missionVariations[modelIndex].insert({ (kvp.first[7] == '_') ? std::string(kvp.first.substr(8)) : std::string(kvp.first), vec });
                }
            }

            bool mergeZones = dataFile.ReadBoolean(section, "MergeZonesWithAreas", false);

            for (auto& kvp : iniData.second)
            {
                if (kvp.first.size() > 1 && (kvp.first[1] < 'a' || kvp.first[1] > 'z')) //also includes interiors
                {
                    auto vec = dataFile.ReadLine(section, kvp.first, READ_PEDS);
                    if (!vec.empty())
                    {
                        pedVars.pedHasVariations.insert(modelIndex);
                        uint64_t zoneName = 0;
                        copyString((char*)&zoneName, kvp.first.data(), std::min<std::size_t>(8, kvp.first.size()));
                        pedVars.variations[zoneName][modelIndex] = mergeZones ? vectorUnion(pedVars.variations[zoneName][modelIndex], vec) : vec;
                    }
                }
            }

            for (unsigned j = 0; j < 6; j++)
            {
                auto vec = dataFile.ReadLine(section, "Wanted" + std::to_string(j+1), READ_PEDS);
                if (vec.empty())
                    continue;
                pedVars.wantedVariations[modelIndex][j] = vec;
            }

            for (const auto& j : pedVars.variations)
                if (auto it = j.second.find(modelIndex); it != j.second.end())
                    for (auto variation : it->second)
                        if (variation > 0 && variation != modelIndex)
                            pedVars.originalModels.insert({ variation, modelIndex });

            for (unsigned int j = 0; j < 9; j++)
            {
                auto groupStart = dataFile.ReadInteger(section, "TimeGroup" + std::to_string(j + 1) + "Start", -1);
                if (groupStart > -1)
                {
                    auto groupEnd = dataFile.ReadInteger(section, "TimeGroup" + std::to_string(j + 1) + "End", -1);
                    if (groupEnd > -1)
                    {
                        auto vec = dataFile.ReadLine(section, "TimeGroup" + std::to_string(j + 1), READ_PEDS);

                        if (!vec.empty())
                        {
                            pedVars.timeGroups[modelIndex].push_back(pedTimeGroup((unsigned short)groupStart, (unsigned short)groupEnd, vec));
                            continue;
                        }
                    }
                }
                break;
            }

            if (dataFile.ReadBoolean(section, "DontInheritBehaviour", false))
                pedVars.dontInheritBehaviourModels.push_back(modelIndex);

            if (dataFile.ReadBoolean(section, "MergeInteriorsWithAreasAndZones", false))
                pedVars.mergeInteriors.push_back(modelIndex);

            if (dataFile.ReadBoolean(section, "DisableOnMission", false))
                pedVars.disableOnMission.push_back(modelIndex);
        }
        
        auto vec = dataFile.ReadLine(section, "WeatherSunny", READ_PEDS);
        if (!vec.empty())
            pedVars.weatherSunny[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherRainy", READ_PEDS);
        if (!vec.empty())
            pedVars.weatherRainy[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherFoggy", READ_PEDS);
        if (!vec.empty())
            pedVars.weatherFoggy[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherSandstorm", READ_PEDS);
        if (!vec.empty())
            pedVars.weatherSandstorm[modelIndex] = vec;

        vec = dataFile.ReadLine(section, "WeatherWindy", READ_PEDS);
        if (!vec.empty())
            pedVars.weatherWindy[modelIndex] = vec;

        int parentVoice = dataFile.ReadInteger(section, "UseParentVoice", -1);
        if (parentVoice > -1)
            pedVars.useParentVoice[modelIndex] = static_cast<bool>(parentVoice);

        std::string animGroupString = dataFile.ReadString(section, "AnimGroup", "");
        if (!animGroupString.empty() && CAnimManager__ms_numAnimAssocDefinitions > 0)
            for (int j = CAnimManager__ms_numAnimAssocDefinitions - 1; j >= 0; j--)
            {
                const char* animString = CAnimManager__GetAnimGroupName(j);
                if (animString != NULL && strcmp(animGroupString.c_str(), animString) == 0)
                {
                    pedVars.animGroups[modelIndex] = (unsigned)j;
                    break;
                }
            }

        vec = dataFile.ReadLine(section, "Voice", READ_PEDS);
        if (!vec.empty())
            pedVars.voices.insert({ modelIndex, vec });
    }

    std::sort(pedVars.dontInheritBehaviourModels.begin(), pedVars.dontInheritBehaviourModels.end());
    std::sort(pedVars.mergeInteriors.begin(), pedVars.mergeInteriors.end());
    std::sort(pedVars.disableOnMission.begin(), pedVars.disableOnMission.end());

    pedOptions.useParentVoices = dataFile.ReadBoolean("Settings", "UseParentVoices", false);
    pedOptions.improveCivilianVariety = dataFile.ReadBoolean("Settings", "ImproveCivilianVariety", false);

    Log::Write("\n");
}

void PedVariations::Process()
{
    bool isRainy = CWeather__IsRainy();
    bool isSandstorm = CWeather::Sandstorm > 0.29;
    bool isFoggy = CWeather::Foggyness > 0.3;
    bool isWindy = CWeather::Wind > 0.29;
    bool isSunny = !isRainy && !isSandstorm && !isFoggy && !isWindy;

    bool weatherChanged = isRainy != weatherState.isRainy ||
                          isSandstorm != weatherState.isSandstorm ||
                          isFoggy != weatherState.isFoggy ||
                          isWindy != weatherState.isWindy ||
                          isSunny != weatherState.isSunny;

    weatherState = { isRainy, isSandstorm, isFoggy, isWindy, isSunny };

    int variationsUpdateQueued = 0;

    static int lastGameTime = -1;
    int gameTime = (CClock__ms_nGameClockHours * 100 + CClock__ms_nGameClockMinutes);

    if (gameTime != lastGameTime)
    {
        lastGameTime = gameTime;
        for (auto& it : pedVars.activeTimeGroups)
            for (auto it2 = it.second.begin(); it2 != it.second.end();)
            {
                unsigned short index = *it2;

                if (!isTimeInRange(gameTime, pedVars.timeGroups[it.first][index].start, pedVars.timeGroups[it.first][index].end))
                {
                    it2 = it.second.erase(it2);
                    variationsUpdateQueued = it.first;
                }
                else
                    ++it2;
            }

        for (const auto& it : pedVars.timeGroups)
            for (unsigned int i = 0; i < it.second.size(); i++)
                if (isTimeInRange(gameTime, it.second[i].start, it.second[i].end))
                    if (pedVars.activeTimeGroups[it.first].insert((unsigned short)i).second == true)
                        variationsUpdateQueued = it.first;
    }

    if (weatherChanged)
    {
        std::string gameTimeString = msprintf("%02d:%02d", CClock__ms_nGameClockHours, CClock__ms_nGameClockMinutes);
        Log::Write("\n[%s] Updating ped variations due to weather change. Current weather: %d %d %d %d %d. Game time: %s\n", getDatetime(false, true, true).c_str(), isRainy, isSandstorm, isFoggy, isWindy, isSunny, gameTimeString.c_str());
        UpdateVariations();
        PedVariations::LogCurrentVariations();
        Log::Write("\n");
        weatherChanged = false;
    }

    if (variationsUpdateQueued > 0)
    {
        std::string gameTimeString = msprintf("%02d:%02d", CClock__ms_nGameClockHours, CClock__ms_nGameClockMinutes);
        Log::Write("Updating ped variations due to model %d time groups. Game time: %s\n", variationsUpdateQueued, gameTimeString.c_str());
        UpdateVariations();
        PedVariations::LogCurrentVariations();
        Log::Write("\n");
        Log::Write("Active time groups\n");
        for (auto it : pedVars.activeTimeGroups)
            if (!it.second.empty())
            {
                Log::Write("%d: ", it.first);
                for (auto j : it.second)
                    Log::Write("%u ", j + 1);
                Log::Write("\n");
            }

        Log::Write("\n\n");
        variationsUpdateQueued = 0;
    }

    std::erase_if(changedVoices, [](std::pair<CPed*, unsigned short> it) {
        return !IsPedPointerValid(it.first);
    });

    ProcessDrugDealers();

    while (!pedVars.stack.empty())
    {
        CPed* ped = pedVars.stack.back();
        pedVars.stack.pop_back();

        if (IsPedPointerValid(ped) && isValidPedId(ped->m_nModelIndex))
        {
            auto it = pedVars.currentVariations.find(ped->m_nModelIndex);
            if (it != pedVars.currentVariations.end() && !it->second.empty() && it->second[0] == 0 && ped->m_nCreatedBy != 2) //Delete models with a 0 id variation
            {
                CVehicle* veh = ped->m_pVehicle;
                if (IsVehiclePointerValid(veh) && veh->m_nCreatedBy != eVehicleCreatedBy::MISSION_VEHICLE && veh->m_pDriver == ped)
                    veh->bFadeOut = true;
                else
                    destroyPed(ped);
            }
        }
    }
}

void PedVariations::ProcessDrugDealers(bool reset)
{
    static int dealersFrames = 0;

    if (reset)
        dealersFrames = 0;
    else
    {
        if (dealersFrames < 10)
            dealersFrames++;

        if (dealersFrames == 10)
        {
            Log::Write("Applying drug dealer fix...\n");
         
            for (auto& it : pedVars.originalModels)
                if (it.first > 300)
                    if (it.second == 28 || it.second == 29 || it.second == 30 || it.second == 254)
                    {
                        Log::Write(addedIDs.contains(it.first) ? "%uSP\n" : "%u\n", it.first);
                        auto findByScmIndex = CExternalScripts__findByScmIndex(CTheScripts__StreamedScripts, 19);

                        CScriptsForBrains__AddNewScriptBrain(CTheScripts__ScriptsForBrains, findByScmIndex, (short)it.first, 100, 0, -1, -1.0);
                    }

            Log::Write("\n");
            dealersFrames = 11;
        }
    }
}

void PedVariations::UpdateVariations()
{
    const CWanted* wanted = FindPlayerWanted(-1);
    const unsigned int wantedLevel = wanted ? (wanted->m_nWantedLevel - (wanted->m_nWantedLevel ? 1 : 0)) : 65535;
    pedVars.currentVariations.clear();

    auto player = FindPlayerPed();
    auto interiorVariations = (player->m_pEnex) ? pedVars.variations.find(*reinterpret_cast<const uint64_t*>(player->m_pEnex)) : pedVars.variations.end();
    auto zoneVariations = pedVars.variations.find(*reinterpret_cast<uint64_t*>(currentZone));
    
    for (auto& modelid : pedVars.pedHasVariations)
    {
        bool modelHasInteriorVariations = false;

        if (interiorVariations != pedVars.variations.end())
            if (auto it = interiorVariations->second.find(modelid); it != interiorVariations->second.end())
            {
                pedVars.currentVariations[modelid] = it->second;
                modelHasInteriorVariations = true;
            }

        if ((!modelHasInteriorVariations || vectorHasId(pedVars.mergeInteriors, modelid)) && zoneVariations != pedVars.variations.end())
            if (auto it = zoneVariations->second.find(modelid); it != zoneVariations->second.end())
                pedVars.currentVariations[modelid] = vectorUnion(it->second, pedVars.currentVariations[modelid]);

        if (wantedLevel < 6)
            if (auto it = pedVars.wantedVariations.find(modelid); it != pedVars.wantedVariations.end())
            {
                if (!it->second[wantedLevel].empty() && !pedVars.currentVariations[modelid].empty())
                    vectorfilterVector(pedVars.currentVariations[modelid], it->second[wantedLevel]);
            }

        if (weatherState.isRainy)
        {
            auto it = pedVars.weatherRainy.find(modelid);
            if (it != pedVars.weatherRainy.end() && !it->second.empty())
                vectorfilterVector(pedVars.currentVariations[modelid], it->second);
        }

        if (weatherState.isSandstorm)
        {
            auto it = pedVars.weatherSandstorm.find(modelid);
            if (it != pedVars.weatherSandstorm.end() && !it->second.empty())
                vectorfilterVector(pedVars.currentVariations[modelid], it->second);
        }

        if (weatherState.isFoggy)
        {
            auto it = pedVars.weatherFoggy.find(modelid);
            if (it != pedVars.weatherFoggy.end() && !it->second.empty())
                vectorfilterVector(pedVars.currentVariations[modelid], it->second);
        }

        if (weatherState.isWindy)
        {
            auto it = pedVars.weatherWindy.find(modelid);
            if (it != pedVars.weatherWindy.end() && !it->second.empty())
                vectorfilterVector(pedVars.currentVariations[modelid], it->second);
        }

        if (weatherState.isSunny)
        {
            auto it = pedVars.weatherSunny.find(modelid);
            if (it != pedVars.weatherSunny.end() && !it->second.empty())
                vectorfilterVector(pedVars.currentVariations[modelid], it->second);
        }

        if (auto it = pedVars.activeTimeGroups.find(modelid); it != pedVars.activeTimeGroups.end())
            for (auto i : it->second)
                vectorfilterVector(pedVars.currentVariations[modelid], pedVars.timeGroups[modelid][i].variations);

        if (auto it = pedVars.missionVariations.find(modelid); it != pedVars.missionVariations.end())
        {
            if (!CTheScripts__IsPlayerOnAMission())
            {
                if (auto it2 = it->second.find("MISSIONGAMEPLAY"); it2 != it->second.end())
                    vectorfilterVector(pedVars.currentVariations[modelid], it2->second);
            }
            else if (auto it2 = it->second.find(currentMission); it2 != it->second.end())
                vectorfilterVector(pedVars.currentVariations[modelid], it2->second);
            else if (auto it3 = it->second.find("MISSIONALL"); it3 != it->second.end())
                vectorfilterVector(pedVars.currentVariations[modelid], it3->second);
        }
    }
}

void PedVariations::DrawDebugInfo(float fontSize, uint32_t debugOptions)
{
    auto* pedPool = CPools::ms_pPedPool;
    if (!pedPool)
        return;

    float fontSizew = RsGlobal.maximumHeight/640.0f * fontSize;
    float fontSizeh = fontSizew * 2.2f;

    // Text style
    CFont::SetBackground(false, false);
    CFont::SetOrientation(ALIGN_CENTER);
    CFont::SetProportional(true);
    CFont::SetFontStyle(FONT_SUBTITLES);
    CFont::SetScale(fontSizew, fontSizeh);
    CFont::SetEdge(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetColor(CRGBA(127, 255, 255, 255));

    for (int i = 0; i < pedPool->m_nSize; ++i)
    {
        CPed* ped = pedPool->GetAt(i);
        if (!IsPedPointerValid(ped) || ped->m_nModelIndex < 7 || !ped->IsAlive() || !isPedVisible(ped))
            continue;

        // Position a little above the ped
        CVector pos = ped->GetPosition();

        RwV3d worldPos;
        worldPos.x = pos.x;
        worldPos.y = pos.y;
        worldPos.z = pos.z + 1.2f;

        RwV3d screenPos;
        float w, h;
        if (!CSprite::CalcScreenCoors(worldPos, &screenPos, &w, &h, true, true))
            continue;

        const float lineOffset = (RsGlobal.maximumHeight / 640.0f) * fontSize * 35.0f;
        float currentOffset = lineOffset;

        if (debugOptions & std::to_underlying(debugDrawPedStats::POINTER))
        {
            std::string line = msprintf("0x%08X", reinterpret_cast<std::uintptr_t>(ped));
            CFont::PrintString(screenPos.x, screenPos.y, line.c_str());
        }

        if (debugOptions & std::to_underlying(debugDrawPedStats::MODEL))
        {
            std::string line = msprintf("%u %s", ped->m_nModelIndex, modelNames.contains(ped->m_nModelIndex) ? modelNames[ped->m_nModelIndex].c_str() : "");
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (debugOptions & std::to_underlying(debugDrawPedStats::CREATED_BY))
        {
            std::string line;
            if (ped->m_nCreatedBy == 0)
                line = "PED_UNKNOWN";
            else if (ped->m_nCreatedBy == 1)
                line = "PED_GAME";
            else if (ped->m_nCreatedBy == 2)
                line = "PED_MISSION";
            else if (ped->m_nCreatedBy == 3)
                line = "PED_GAME_MISSION";

            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (debugOptions & std::to_underlying(debugDrawPedStats::PED_TYPE))
        {
            std::string line;
            static constexpr std::array<std::string_view, 32> pedTypeNames{"PED_TYPE_PLAYER1", "PED_TYPE_PLAYER2", "PED_TYPE_PLAYER_NETWORK", "PED_TYPE_PLAYER_UNUSED",
                                                                           "PED_TYPE_CIVMALE", "PED_TYPE_CIVFEMALE", "PED_TYPE_COP", "PED_TYPE_GANG1", "PED_TYPE_GANG2",
                                                                           "PED_TYPE_GANG3", "PED_TYPE_GANG4", "PED_TYPE_GANG5", "PED_TYPE_GANG6", "PED_TYPE_GANG7",
                                                                           "PED_TYPE_GANG8", "PED_TYPE_GANG9", "PED_TYPE_GANG10", "PED_TYPE_DEALER", "PED_TYPE_MEDIC",
                                                                           "PED_TYPE_FIREMAN", "PED_TYPE_CRIMINAL", "PED_TYPE_BUM", "PED_TYPE_PROSTITUTE", "PED_TYPE_SPECIAL",
                                                                           "PED_TYPE_MISSION1", "PED_TYPE_MISSION2", "PED_TYPE_MISSION3", "PED_TYPE_MISSION4", 
                                                                           "PED_TYPE_MISSION5", "PED_TYPE_MISSION6", "PED_TYPE_MISSION7", "PED_TYPE_MISSION8"};

            if (ped->m_nPedType >= PED_TYPE_PLAYER1 && ped->m_nPedType <= PED_TYPE_MISSION8)
                line = pedTypeNames[ped->m_nPedType];
            else
                line = "UNKNOWN PED TYPE";
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (debugOptions & std::to_underlying(debugDrawPedStats::PROOFS))
        {
            std::string proofs = msprintf("%s%s%s%s%s%s", ped->bBulletProof ? " BP" : "",
                                                          ped->bFireProof ? " FP" : "",
                                                          ped->bCollisionProof ? " CP" : "",
                                                          ped->bMeleeProof ? " MP" : "",
                                                          ped->bExplosionProof ? " EP" : "",
                                                          ped->bInvulnerable ? " WP" : "");
            if (!proofs.empty())
            {
                std::string line = msprintf("Proofs:%s", proofs.c_str());
                CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
                currentOffset += lineOffset;
            }
        }

        if (debugOptions & std::to_underlying(debugDrawPedStats::HEALTH))
        {
            std::string line = msprintf("Health: %.0f/%.0f", ped->m_fHealth, ped->m_fMaxHealth);
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (debugOptions & std::to_underlying(debugDrawPedStats::ARMOUR) && ped->m_fArmour > 0.0f)
        {
            std::string line = msprintf("Armour: %.0f", ped->m_fArmour);
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }
       
        if (auto it = pedVars.originalModels.find(ped->m_nModelIndex); (debugOptions & std::to_underlying(debugDrawPedStats::MODEL)) && it != pedVars.originalModels.end())
        {
            std::string line = "Parent model: " + std::to_string(it->second);
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, line.c_str());
            currentOffset += lineOffset;
        }

        if (auto it = changedVoices.find(ped); (debugOptions & std::to_underlying(debugDrawPedStats::VOICE)) && it != changedVoices.end())
        {
            std::string buffer = msprintf("Voice: %u", it->second);
            CFont::PrintString(screenPos.x, screenPos.y + currentOffset, buffer.c_str());
            currentOffset += lineOffset;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  LOGGING   ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

void PedVariations::LogCurrentVariations()
{
    if (!Log::Write("pedCurrentVariations"))
        return;

    if (pedVars.currentVariations.empty())
        Log::Write(" is empty\n");
    else
        Log::Write("\n");

    for (auto it : pedVars.currentVariations)
        if (!it.second.empty())
        {
            Log::Write("%d: ", it.first);
            for (auto j : it.second)
            {
                const char* suffix = " ";
                if (addedIDs.contains(j))
                    suffix = "SP ";
                Log::Write("%u%s", j, suffix);
            }
            Log::Write("\n");
        }
}

void PedVariations::LogDataFile()
{
    if (!fileExists(dataFileName))
        Log::Write("\n%s not found!\n\n", dataFileName);
    else
    {
        Log::Write("%s\n", printFilenameWithBorder(dataFileName, '#').c_str());
        Log::Write("%s\n", fileToString(dataFileName).c_str());
    }
}

void PedVariations::LogVariations()
{
    if (!Log::Write("Ped Variations:\n"))
        return;

    std::map<unsigned short, std::set<unsigned short>> variationsMap;
    for (const auto& it : pedVars.variations)
    {
        for (const auto &i : it.second)
            for (auto j : i.second)
                variationsMap[i.first].insert(j);
    }

    for (const auto& i : variationsMap)
    {
        Log::Write("%u: ", i.first);
        for (auto j : i.second)
        {
            const char* suffix = " ";
            if (addedIDs.contains(j))
                suffix = "SP ";
            Log::Write("%u%s", j, suffix);
        }
        Log::Write("\n");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////  CALL HOOKS    ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

__declspec(noinline) int __cdecl getKillsByPlayer(int player)
{
    int sum = 0;

    for (int i = maxPedID * player; i < maxPedID * (player+1); i++)
        sum += destroyedModelCounters[i];

    return sum;
}

__declspec(noinline) void __fastcall SetModelIndexHooked(CEntity* _this, void*, const int index)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (index < 7 || index > 65535 || (vectorHasId(pedVars.disableOnMission, index) && CTheScripts__IsPlayerOnAMission()))
        return originalCall.callMethod(_this, index);

    auto it = pedVars.currentVariations.find((unsigned short)index);
    if (isValidPedId(index) && it != pedVars.currentVariations.end() && !it->second.empty())
    {
        const unsigned short newModel = vectorGetRandom(it->second);
        if (newModel > 0 && newModel != index)
        {
            if (auto loadState = loadModel(newModel, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
            {
                Log::Write("Error loading ped model %d (%s) %s. Using original model %d.\n", newModel, modelNames.contains(newModel) ? modelNames[newModel].c_str() : "", getLoadStateString(loadState), index);
                return originalCall.callMethod(_this, index);
            }
                    
            originalCall.callMethod(_this, newModel);

            Log::WriteVerbose("Ped 0x%08X index %d was replaced with model %u\n", reinterpret_cast<uint32_t>(_this), index, newModel);

            if (!vectorHasId(pedVars.dontInheritBehaviourModels, index))
                _this->m_nModelIndex = (unsigned short)index;
            variationModel = newModel;
            return;
        }
    }

    originalCall.callMethod(_this, index);
}

__declspec(noinline) void __fastcall UpdateRpHAnimHooked(CPed* entity)
{
    const auto originalCall = captureCurrentOriginalCall();
    originalCall.callMethod(entity);

    if (auto it = pedVars.animGroups.find(variationModel > 0 ? variationModel : entity->m_nModelIndex); it != pedVars.animGroups.end())
        entity->m_nAnimGroup = it->second;

    if (variationModel > 0)
        entity->m_nModelIndex = variationModel;
    variationModel = 0;
}

__declspec(noinline) char __fastcall CAEPedSpeechAudioEntity__InitialiseHooked(CAEPedSpeechAudioEntity* _this, void*, CPed* ped)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (ped != NULL)
    {
        const auto currentModel = ped->m_nModelIndex;
        unsigned short newModel = 0;

        bool useParentVoice = false;

        if (auto it = pedVars.useParentVoice.find(ped->m_nModelIndex); it != pedVars.useParentVoice.end())
            useParentVoice = it->second;
        else
            useParentVoice = pedOptions.useParentVoices;

        if (useParentVoice)
        {
            auto it = pedVars.originalModels.find(ped->m_nModelIndex);
            if (it != pedVars.originalModels.end())
                newModel = it->second;
        }

        auto it = pedVars.voices.find(ped->m_nModelIndex);
        if (it != pedVars.voices.end() && !it->second.empty())
            newModel = vectorGetRandom(it->second);

        if (newModel > 0)
        {
            changedVoices[ped] = newModel;
            ped->m_nModelIndex = newModel;
            char retVal = originalCall.callMethodAndReturn<char>(_this, ped);
            ped->m_nModelIndex = currentModel;
            return retVal;
        }
    }

    return originalCall.callMethodAndReturn<char>(_this, ped);
}

__declspec(noinline) CPhysical* __fastcall CPhysicalHooked(CPed* _this)
{
    const auto originalCall = captureCurrentOriginalCall();
    changedVoices.erase(_this);
    CPhysical* retVal = originalCall.callMethodAndReturn<CPhysical*>(_this);
    pedVars.stack.push_back(_this);
    return retVal;
}

//Improper fix for crash 0x68FB5C
__declspec(noinline) void* __fastcall CreateNextSubTaskHooked(CTaskComplexCopInCar* _this, void*, CPed* ped)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (_this == NULL)
        Log::Write("CreateNextSubTaskHooked _this is NULL. ped is 0x%08X\n", ped);
    else if (ped == NULL)
        Log::Write("CreateNextSubTaskHooked ped is NULL. _this is 0x%08X\n", _this);
    else if (_this->m_pVehicle == NULL)
    {
        Log::Write("CTaskComplexCopInCar 0x%08X of ped 0x%08X with model index %u has NULL vehicle pointer.\n", _this, ped, ped->m_nModelIndex);
        auto subtask = _this->GetSubTask();
        if (subtask && subtask->GetId() == TASK_SIMPLE_CAR_DRIVE)
        {
            if (ped->m_pVehicle)
                ped->m_pVehicle->m_autoPilot.m_nCarMission = MISSION_NONE;
            else
                Log::Write("CTaskComplexCopInCar ped->m_pVehicle is NULL.\n");

            uint8_t originalData[7];
            injector::ReadMemoryRaw(0x68FB5C, originalData, 7, true);
            injector::MakeNOP(0x68FB5C, 7);
            auto retVal = originalCall.callMethodAndReturn<void*>(_this, ped);
            injector::WriteMemoryRaw(0x68FB5C, originalData, 7, true);
            return retVal;
        }
    }

    return originalCall.callMethodAndReturn<void*>(_this, ped);
}

__declspec(noinline) int __cdecl ChooseCivilianOccupationHooked(char male, char female, int animType, int ignoreModelIndex, int statType,
    char a6, char a7, char checkAttractor, char* attrName)
{
    const auto originalCall = captureCurrentOriginalCall();

    ignoreCivilianVariety = true;

    return originalCall.callAndReturn<int>(male, female, animType, ignoreModelIndex, statType, a6, a7, checkAttractor, attrName);
}

__declspec(noinline) bool __cdecl PedIsAcceptableInCurrentZoneHooked(int a1)
{
    const auto originalCall = captureCurrentOriginalCall();

    if (ignoreCivilianVariety)
    {
        ignoreCivilianVariety = false;
        return originalCall.callAndReturn<bool>(a1);
    }

    for (CPed *ped : CPools::ms_pPedPool)
    {
        if (ped && ped->m_nModelIndex == a1)
            return false;
    }

    return originalCall.callAndReturn<bool>(a1);
}

__declspec(noinline) int __cdecl ChooseCivilianOccupationForVehicleHooked(char male, CVehicle* a2)
{
    const auto originalCall = captureCurrentOriginalCall();

    auto vehDrivers = { 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32, 33, 34, 
                        35, 36, 37, 40, 43, 44, 45, 46, 47, 48, 50, 55, 56, 57, 58, 59, 60, 66, 67, 68, 69, 71, 72, 73, 
                        82, 83, 84, 90, 91, 93, 94, 95, 97, 98, 100, 101, 128, 131, 132, 133, 134, 135, 136, 137, 138, 
                        139, 140, 141, 142, 143, 147, 148, 150, 151, 153, 154, 157, 158, 159, 160, 161, 162, 168, 169, 
                        170, 181, 182, 183, 184, 185, 186, 187, 188, 198, 199, 200, 201, 202, 206, 210, 211, 212, 213, 
                        215, 216, 217, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 233, 234, 235, 236, 240, 
                        241, 242, 247, 248, 250, 255, 260, 261, 262, 263 };


    auto modelid = originalCall.callAndReturn<int>(male, a2);
    CVehicleModelInfo* vehModelInfo = (CVehicleModelInfo*)CModelInfo::GetModelInfo(a2->m_nModelIndex);

    if (modelid < 9)
    {
        auto specificDriver = CPopulation::FindSpecificDriverModelForCar_ToUse(a2->m_nModelIndex);
        if (specificDriver > 0 && loadModel(specificDriver, PRIORITY_REQUEST, true) == LOADSTATE_LOADED)
            return specificDriver;


        for (auto i : vehDrivers)
        {
            auto mInfo = CModelInfo::GetModelInfo(i);
            if (mInfo && vehModelInfo && mInfo->GetModelType() == MODEL_INFO_PED && CPopCycle::IsPedAppropriateForCurrentZone(i) && (canPedDriveVeh(i, a2->m_nModelIndex) || a2->m_nModelIndex == 422))
            {
                bool modelExists = false;
                for (CPed* ped : CPools::ms_pPedPool)
                    if (ped && ped->m_nModelIndex == i)
                    {
                        modelExists = true;
                        break;
                    }

                if (!modelExists)
                    if (auto loadState = loadModel(i, PRIORITY_REQUEST, true); loadState != LOADSTATE_LOADED)
                    {
                        auto modelName = modelNames.find(static_cast<unsigned short>(i));
                        Log::Write("Error loading ped model %d (%s) %s.\n", i, modelName != modelNames.end() ? modelName->second.c_str() : "", getLoadStateString(loadState));
                    }
                    else
                        return i;
            }
        }
    }
   
    std::pair<unsigned short, unsigned short> leastUsedModel = { 7, 65535 };

    for (auto i : vehDrivers)
    {
        auto mInfo = CModelInfo::GetModelInfo(i);
        if (mInfo && mInfo->m_nRefCount > 0 && mInfo->m_nRefCount <= leastUsedModel.second && canPedDriveVeh(i, a2->m_nModelIndex))
        {
            if (mInfo->m_nRefCount == leastUsedModel.second)
            {
                if (rand<bool>())
                    leastUsedModel.first = static_cast<unsigned short>(i);
            }
            else
            {
                leastUsedModel.first = static_cast<unsigned short>(i);
                leastUsedModel.second = mInfo->m_nRefCount;
            }
        }
    }

    return leastUsedModel.first;
}

void PedVariations::InstallHooks(bool enableSpecialPeds)
{
    if (enableSpecialPeds)
    {
        bool gameHOODLUM = isGameHOODLUM();
        bool notModified = true;

        //Count of killable model IDs
        if (!memoryMatchesOriginalExe(0x43DE6C, 8) ||
            !memoryMatchesOriginalExe(0x43DF5B, 8) ||
            !memoryMatchesOriginalExe((gameHOODLUM ? 0x1561634U : 0x43D6A4), 7) ||
            !memoryMatchesOriginalExe((gameHOODLUM ? 0x1564C2BU : 0x43D6CB), 8))
        {
            notModified = false;
        }

        if (notModified && maxPedID > 800)
        {
            destroyedModelCounters.resize(maxPedID * 2);

            injector::WriteMemory<int16_t*>(0x43DE70, &destroyedModelCounters[0], true);
            injector::WriteMemory<int16_t*>(0x43DF5F, &destroyedModelCounters[0], true);

            if (gameHOODLUM)
            {
                injector::WriteMemory<int16_t*>(0x1561637, &destroyedModelCounters[0], true);
                injector::WriteMemory<uint32_t>(0x156163C, maxPedID, true);

                injector::WriteMemory<int16_t*>(0x1564C2F, &destroyedModelCounters[0], true);
            }
            else
            {
                injector::WriteMemory<int16_t*>(0x43D6A7, &destroyedModelCounters[0], true);
                injector::WriteMemory<uint32_t>(0x43D6AC, maxPedID, true);

                injector::WriteMemory<int16_t*>(0x43D6CF, &destroyedModelCounters[0], true);
            }

            hookCall<0x47360D>(getKillsByPlayer, "CDarkel::FindTotalPedsKilledByPlayer");
        }
        else
            Log::Write("Count of killable model IDs was not increased. %s\n", (LoadedModules::IsModLoaded(MOD_FLA) ? "FLA is loaded." : "FLA is NOT loaded."));
    }


    hookSharedCall<0x5E4890, SetModelIndexHooked>("CEntity::SetModelIndex"); //CPed::SetModelIndex
    hookSharedCall<0x5E49EF, UpdateRpHAnimHooked>("CEntity::UpdateRpHAnim"); //CPed::SetModelIndex

    hookSharedCall<0x5DDBB8, CAEPedSpeechAudioEntity__InitialiseHooked>("CAEPedSpeechAudioEntity::Initialise"); //CCivilianPed
    hookSharedCall<0x5DDD24, CAEPedSpeechAudioEntity__InitialiseHooked>("CAEPedSpeechAudioEntity::Initialise"); //CCopPed
    hookSharedCall<0x5DE388, CAEPedSpeechAudioEntity__InitialiseHooked>("CAEPedSpeechAudioEntity::Initialise"); //CEmergencyPed

    hookSharedCall<0x5E8052, CPhysicalHooked>("CPhysical::CPhysical"); //CPed::CPed

    hookSharedCall<0x870A4C, CreateNextSubTaskHooked>("CTaskComplexCopInCar::CreateNextSubTask", true);

    if (pedOptions.improveCivilianVariety)
    {
        hookSharedCall<0x48335E, ChooseCivilianOccupationHooked>("CPopulation::ChooseCivilianOccupation"); //0376 CREATE_RANDOM_CHAR
        hookSharedCall<0x61302B, PedIsAcceptableInCurrentZoneHooked>("CPopCycle::PedIsAcceptableInCurrentZone"); //CPopulation::ChooseCivilianOccupation
        hookSharedCall<0x61330D, PedIsAcceptableInCurrentZoneHooked>("CPopCycle::PedIsAcceptableInCurrentZone"); //CPopulation::ChooseCivilianOccupationForVehicle
        hookSharedCall<0x613B32, ChooseCivilianOccupationForVehicleHooked>("CPopulation::ChooseCivilianOccupationForVehicle"); //CPopulation::AddPedInCar
    }
}
