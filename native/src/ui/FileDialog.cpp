#include "FileDialog.h"

#include <commdlg.h>
#include <vector>

namespace {

std::wstring NarrowToWide(const std::string& narrow) {
    if (narrow.empty()) return {};
    int size = MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, out.data(), size);
    return out;
}

// Builds a Win32 "Description (*.ext)\0*.ext\0\0" double-null-terminated filter string.
std::vector<wchar_t> BuildFilter(const std::string& filterName, const std::string& extension) {
    std::wstring label = NarrowToWide(filterName) + L" (*." + NarrowToWide(extension) + L")";
    std::wstring pattern = L"*." + NarrowToWide(extension);

    std::vector<wchar_t> filter;
    filter.insert(filter.end(), label.begin(), label.end());
    filter.push_back(L'\0');
    filter.insert(filter.end(), pattern.begin(), pattern.end());
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    return filter;
}

} // namespace

std::string WideToNarrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

bool ShowSaveFileDialog(HWND owner, const std::string& filterName, const std::string& extension,
                         const std::string& defaultName, std::wstring& outPath) {
    std::vector<wchar_t> filter = BuildFilter(filterName, extension);
    std::wstring wideDefault = NarrowToWide(defaultName);
    std::wstring wideExt = NarrowToWide(extension);

    wchar_t buffer[MAX_PATH];
    wcsncpy_s(buffer, wideDefault.c_str(), MAX_PATH - 1);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.data();
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = wideExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&ofn)) {
        outPath = buffer;
        return true;
    }
    return false;
}

bool ShowOpenFileDialog(HWND owner, const std::string& filterName, const std::string& extension,
                         std::wstring& outPath) {
    std::vector<wchar_t> filter = BuildFilter(filterName, extension);

    wchar_t buffer[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.data();
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        outPath = buffer;
        return true;
    }
    return false;
}
