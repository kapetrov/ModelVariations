#include "Memory.hpp"

#include "Helpers.hpp"
#include "Log.hpp"

#include <algorithm>
#include <psapi.h>

namespace
{
    std::vector<OriginalExeSection>& originalExeSections()
    {
        static std::vector<OriginalExeSection> sections;
        return sections;
    }
}

bool loadOriginalExeSections(const char* filePath, std::span<const int> sectionIndices, std::span<const std::uintptr_t> sectionAddresses)
{
    if (sectionIndices.empty() || sectionIndices.size() != sectionAddresses.size())
    {
        Log::Write("Error loading original executable sections: invalid section mapping.\n");
        return false;
    }

    std::vector<OriginalExeSection> loadedSections(sectionIndices.size());

    for (std::size_t i = 0; i < sectionIndices.size(); ++i)
    {
        auto& section = loadedSections[i];
        section.address = sectionAddresses[i];

        if (!loadPESection(filePath, sectionIndices[i], &section.data, nullptr))
        {
            Log::Write("Error loading original executable section %d.\n", sectionIndices[i]);
            return false;
        }
    }

    originalExeSections().swap(loadedSections);
    return true;
}

std::span<const OriginalExeSection> getOriginalExeSections() noexcept
{
    const auto& sections = originalExeSections();
    return { sections.data(), sections.size() };
}

bool memoryMatchesOriginalExe(std::uintptr_t address, std::size_t size) noexcept
{
    // Some hooks are validated separately and intentionally request no byte comparison.
    if (size == 0)
        return true;

    for (const auto& section : originalExeSections())
    {
        if (address < section.address)
            continue;

        const std::size_t offset = address - section.address;
        if (offset > section.data.size() || size > section.data.size() - offset)
            continue;

        const auto* actual = reinterpret_cast<const unsigned char*>(address);
        return std::equal(actual, actual + size, section.data.begin() + offset);
    }

    return false;
}

size_t getMemoryUsage()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return pmc.PrivateUsage;

    return 0;
}

bool isAddressValid(std::uintptr_t address)
{
    if (address == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi)) == 0)
        return false; // Query failed

    return (mbi.State == MEM_COMMIT) && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

bool isAddressValid(void* address)
{
    return isAddressValid(reinterpret_cast<std::uintptr_t>(address));
}

