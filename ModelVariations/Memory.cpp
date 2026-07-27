#include "Memory.hpp"

#include <psapi.h>

bool memoryMatches(std::uintptr_t address, const std::uint8_t* expected, std::size_t size) noexcept
{
    const auto* actual = reinterpret_cast<const std::uint8_t*>(address);

    for (std::size_t i = 0; i < size; ++i)
    {
        if (actual[i] != expected[i])
            return false;
    }

    return true;
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

