#pragma once
// Native Win32 save/open file dialogs, factored out of main.cpp so exporters (e.g.
// TextFileExporter's "Export channels to text file" button) can use the same helpers
// instead of each reinventing GetSaveFileNameW/GetOpenFileNameW boilerplate.
#include <string>
#include <windows.h>

// `filterName`/`extension` describe one file type, e.g. ("Channel Information", "chinfo")
// produces the filter "Channel Information (*.chinfo)" / pattern "*.chinfo". `defaultName`
// pre-fills the file name field (e.g. "channelinfo.chinfo"). Returns false if the user
// cancels.
bool ShowSaveFileDialog(HWND owner, const std::string& filterName, const std::string& extension,
                         const std::string& defaultName, std::wstring& outPath);

bool ShowOpenFileDialog(HWND owner, const std::string& filterName, const std::string& extension,
                         std::wstring& outPath);

// Windows narrow<->wide conversion uses the ANSI codepage (CP_ACP) rather than UTF-8,
// to match what the CRT's narrow std::ifstream/std::ofstream actually open on this
// platform. Non-ASCII paths are a known limitation.
std::string WideToNarrow(const std::wstring& wide);
