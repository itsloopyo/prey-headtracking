#include "Watchpoint.hpp"

#include "Logging.hpp"

#include <windows.h>
#include <psapi.h>

#include <atomic>

namespace preyht::watch {
namespace {

constexpr uintptr_t kPageSize = 0x1000;

std::atomic<uintptr_t> g_pageBase{0};
uintptr_t              g_moduleBase = 0;
uintptr_t              g_moduleEnd  = 0;

/// EXCEPTION_INFORMATION[0] for an access violation: 0 read, 1 write.
constexpr ULONG_PTR kAccessViolationWrite = 1;

LONG CALLBACK PageHandler(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (info->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    if (info->ExceptionRecord->ExceptionInformation[0] != kAccessViolationWrite) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const uintptr_t page = g_pageBase.load(std::memory_order_acquire);
    if (page == 0) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t hit = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
    if (hit < page || hit >= page + kPageSize) return EXCEPTION_CONTINUE_SEARCH;

    const uintptr_t rip = static_cast<uintptr_t>(info->ContextRecord->Rip);
    if (rip >= g_moduleBase && rip < g_moduleEnd) {
        PHT_LOG(Info, "lightwrite: RVA 0x%llX wrote %p",
                static_cast<unsigned long long>(rip - g_moduleBase),
                reinterpret_cast<void*>(hit));
    } else {
        PHT_LOG(Info, "lightwrite: %p (outside PreyDll) wrote %p",
                reinterpret_cast<void*>(rip), reinterpret_cast<void*>(hit));
    }

    // One shot - let the page go back to normal so the game keeps running.
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<void*>(page), kPageSize, PAGE_READWRITE, &old);
    g_pageBase.store(0, std::memory_order_release);
    return EXCEPTION_CONTINUE_EXECUTION;
}

/// So a faulting instruction can be reported as an RVA the RE notes can be
/// matched against, rather than a load-order-dependent absolute address.
void CachePreyDllBounds() {
    HMODULE mod = GetModuleHandleW(L"PreyDll.dll");
    if (mod == nullptr) return;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi))) return;
    g_moduleBase = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    g_moduleEnd  = g_moduleBase + mi.SizeOfImage;
}

}  // namespace

bool ArmPageWrite(uintptr_t address) {
    if (address == 0) return false;
    CachePreyDllBounds();

    const uintptr_t page = address & ~(kPageSize - 1);
    void* handler = AddVectoredExceptionHandler(1, &PageHandler);
    if (handler == nullptr) return false;
    g_pageBase.store(page, std::memory_order_release);

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(page), kPageSize, PAGE_READONLY, &old)) {
        g_pageBase.store(0, std::memory_order_release);
        RemoveVectoredExceptionHandler(handler);
        return false;
    }
    PHT_LOG(Info, "Watching the page holding %p for the next write, on any thread.",
            reinterpret_cast<void*>(address));
    return true;
}

}  // namespace preyht::watch
