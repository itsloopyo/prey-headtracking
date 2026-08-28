#pragma once

#include <cstdint>
#include <type_traits>

#include <windows.h>

namespace preyht {

/// SEH-guarded copy of a trivially-copyable value out of process memory that may
/// be freed underneath us (e.g. an engine struct released on a level transition).
/// Returns false instead of faulting, so the caller can drop its cached pointer
/// and re-resolve. Free function with no unwinding objects in scope, so __try is
/// legal here (MSVC C2712 forbids it alongside C++ object destruction).
template <typename T>
bool SafeRead(uintptr_t addr, T& out) {
    static_assert(std::is_trivially_copyable_v<T>, "SafeRead target must be trivially copyable");
    __try {
        out = *reinterpret_cast<const T*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

/// SEH-guarded write of a trivially-copyable value into process memory that may
/// be freed underneath us. Returns false instead of faulting.
template <typename T>
bool SafeWrite(uintptr_t addr, const T& in) {
    static_assert(std::is_trivially_copyable_v<T>, "SafeWrite target must be trivially copyable");
    __try {
        *reinterpret_cast<T*>(addr) = in;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace preyht
