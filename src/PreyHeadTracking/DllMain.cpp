#include <windows.h>

#include "Framework.hpp"

namespace {

DWORD WINAPI InitThread(LPVOID) {
    // We run off the loader lock; sleep briefly so the host has a chance to
    // finish its own module init before we start probing D3D.
    Sleep(100);
    preyht::Framework::Get().Initialize();
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            CloseHandle(CreateThread(nullptr, 0, &InitThread, hModule, 0, nullptr));
            break;
        case DLL_PROCESS_DETACH:
            // lpReserved is non-null when the whole process is going away. Every
            // other thread has already been terminated by then, so the receiver
            // and hotkey threads this would join can never run again - and one of
            // them may have been killed holding the log mutex, which would hang
            // the game on exit. Windows reclaims all of it; the log file flushes
            // on every write, so nothing is lost by leaving quietly. Teardown is
            // only correct for a real FreeLibrary, which an ASI never gets.
            if (lpReserved == nullptr) preyht::Framework::Get().Shutdown();
            break;
        default: break;
    }
    return TRUE;
}
