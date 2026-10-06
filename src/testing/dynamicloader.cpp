#include "stride/testing/dynamicloader.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace strd::test {

DynamicLoader::DynamicLoader(const std::string& libraryPath) {
#if defined(_WIN32)
    m_handle = LoadLibraryA(libraryPath.c_str());
    if (!m_handle) {
        DWORD error = GetLastError();
        m_errorMsg = "Failed to load DLL: " + libraryPath + " (Error code: " + std::to_string(error) + ")";
    }
#else
    m_handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle) {
        const char* err = dlerror();
        m_errorMsg = err ? err : "Failed to load shared library";
    }
#endif
}

DynamicLoader::~DynamicLoader() {
    if (m_handle) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(m_handle));
#else
        dlclose(m_handle);
#endif
        m_handle = nullptr;
    }
}

DynamicLoader::DynamicLoader(DynamicLoader&& other) noexcept
    : m_handle(other.m_handle), m_errorMsg(std::move(other.m_errorMsg)) {
    other.m_handle = nullptr;
}

DynamicLoader& DynamicLoader::operator=(DynamicLoader&& other) noexcept {
    if (this != &other) {
        if (m_handle) {
#if defined(_WIN32)
            FreeLibrary(static_cast<HMODULE>(m_handle));
#else
            dlclose(m_handle);
#endif
        }
        m_handle = other.m_handle;
        m_errorMsg = std::move(other.m_errorMsg);
        other.m_handle = nullptr;
    }
    return *this;
}

bool DynamicLoader::isLoaded() const {
    return m_handle != nullptr;
}

const std::string& DynamicLoader::getErrorMessage() const {
    return m_errorMsg;
}

void* DynamicLoader::getRawSymbol(const std::string& symbolName) const {
    if (!m_handle) return nullptr;
#if defined(_WIN32)
    FARPROC proc = GetProcAddress(static_cast<HMODULE>(m_handle), symbolName.c_str());
    return reinterpret_cast<void*>(proc);
#else
    return dlsym(m_handle, symbolName.c_str());
#endif
}

} // namespace strd::test
