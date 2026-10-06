#ifndef STRIDE_TESTING_DYNAMICLOADER_HPP
#define STRIDE_TESTING_DYNAMICLOADER_HPP

#include <string>

namespace strd::test {

class DynamicLoader {
public:
    explicit DynamicLoader(const std::string& libraryPath);
    ~DynamicLoader();

    // Non-copyable, movable
    DynamicLoader(const DynamicLoader&) = delete;
    DynamicLoader& operator=(const DynamicLoader&) = delete;

    DynamicLoader(DynamicLoader&& other) noexcept;
    DynamicLoader& operator=(DynamicLoader&& other) noexcept;

    bool isLoaded() const;
    const std::string& getErrorMessage() const;

    void* getRawSymbol(const std::string& symbolName) const;

    template <typename FuncPtr>
    FuncPtr getSymbol(const std::string& symbolName) const {
        return reinterpret_cast<FuncPtr>(getRawSymbol(symbolName));
    }

private:
    void* m_handle{nullptr};
    std::string m_errorMsg;
};

} // namespace strd::test

#endif // STRIDE_TESTING_DYNAMICLOADER_HPP
