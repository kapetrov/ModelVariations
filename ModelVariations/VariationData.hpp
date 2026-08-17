#pragma once

#include <array>
#include <unordered_map>
#include <vector>


extern std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>> variations;
extern std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>>::iterator currentZoneVariations;

int __stdcall getVariationOriginalModel(int);
void resetOriginalModels();
void setOriginalModel(int model, int originalModel);
