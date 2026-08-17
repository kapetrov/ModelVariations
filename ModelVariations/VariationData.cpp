#include "VariationData.hpp"


std::array<unsigned short, 65536> originalModels{};


std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>> variations;
std::unordered_map<uint64_t, std::unordered_map<unsigned short, std::vector<unsigned short>>>::iterator currentZoneVariations = variations.end();


__declspec(naked) int __stdcall getVariationOriginalModel(int)
{
    __asm
    {
        push    ecx

        mov     eax, [esp + 8]
        mov     ecx, eax
        bswap   ecx
        jcxz    in_range

        pop     ecx
        ret     4

in_range:
        movzx   eax, word ptr[originalModels + eax * 2]
        pop     ecx
        ret     4
    }
}

void resetOriginalModels()
{
    for (size_t i = 0; i < 65536; i++)
        originalModels[i] = static_cast<unsigned short>(i);
}

void setOriginalModel(int model, int originalModel)
{
    if (model > 0 && model < 65536 && originalModel > 0 && originalModel < 65536)
        originalModels[static_cast<unsigned short>(model)] = static_cast<unsigned short>(originalModel);
}
