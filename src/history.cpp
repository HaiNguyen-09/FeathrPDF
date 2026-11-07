#include "history.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>

static std::string WStringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], sizeNeeded, NULL, NULL);
    return str;
}

static std::wstring Utf8ToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], sizeNeeded);
    return wstr;
}

static std::wstring NormalizePath(const std::wstring& path) {
    std::wstring res = path;
    for (auto& c : res) {
        if (c == L'/') c = L'\\';
    }
    size_t first = res.find_first_not_of(L" \t\r\n\"");
    if (first == std::wstring::npos) return L"";
    size_t last = res.find_last_not_of(L" \t\r\n\"");
    return res.substr(first, last - first + 1);
}

static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

static std::string StripQuotes(const std::string& str) {
    std::string s = Trim(str);
    if (s.length() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.length() - 2);
    }
    return s;
}

SessionHistory::SessionHistory() {
}

SessionHistory::~SessionHistory() {
}

std::wstring SessionHistory::GetIniPath() const {
    WCHAR szPath[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, szPath))) {
        std::wstring dir = std::wstring(szPath) + L"\\FeathrPDF";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\session.ini";
    }
    WCHAR exePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring s(exePath);
    size_t lastSlash = s.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        return s.substr(0, lastSlash) + L"\\session.ini";
    }
    return L".\\session.ini";
}

void SessionHistory::Load() {
    m_items.clear();
    std::wstring iniPath = GetIniPath();

    std::ifstream file(iniPath.c_str(), std::ios::binary);
    if (!file.is_open()) return;

    std::string line;
    SessionItem currentItem;
    bool hasItem = false;
    bool isSettings = false;

    auto FlushCurrent = [&]() {
        if (hasItem && !currentItem.filePath.empty()) {
            // Discard legacy truncated entries (like "C")
            if (currentItem.filePath.length() > 3 || 
                currentItem.filePath.find(L'\\') != std::wstring::npos || 
                currentItem.filePath.find(L'/') != std::wstring::npos) {
                if (currentItem.lastPage < 1) currentItem.lastPage = 1;
                if (currentItem.zoom < 0.25f || currentItem.zoom > 6.0f) currentItem.zoom = 1.0f;
                m_items.push_back(currentItem);
            }
        }
        currentItem = SessionItem();
        hasItem = false;
    };

    while (std::getline(file, line)) {
        // Strip carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            FlushCurrent();
            std::string sec = StripQuotes(line.substr(1, line.length() - 2));
            if (_stricmp(sec.c_str(), "Settings") == 0) {
                isSettings = true;
                hasItem = false;
            } else {
                isSettings = false;
                std::wstring wsec = Utf8ToWString(sec);
                if (wsec.find(L'\\') != std::wstring::npos || wsec.find(L'/') != std::wstring::npos) {
                    currentItem.filePath = NormalizePath(wsec);
                }
                hasItem = true;
            }
        } else if (isSettings) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = Trim(line.substr(0, eqPos));
                std::string val = StripQuotes(line.substr(eqPos + 1));
                if (_stricmp(key.c_str(), "DarkMode") == 0) {
                    m_isDarkMode = (val == "1" || _stricmp(val.c_str(), "true") == 0);
                }
            }
        } else if (hasItem) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string key = Trim(line.substr(0, eqPos));
                std::string val = StripQuotes(line.substr(eqPos + 1));
                if (_stricmp(key.c_str(), "Path") == 0) {
                    std::wstring wpath = Utf8ToWString(val);
                    if (!wpath.empty()) {
                        currentItem.filePath = NormalizePath(wpath);
                    }
                } else if (_stricmp(key.c_str(), "LastPage") == 0) {
                    try { currentItem.lastPage = std::stoi(val); } catch (...) {}
                } else if (_stricmp(key.c_str(), "ScrollY") == 0) {
                    try { currentItem.scrollY = std::stoi(val); } catch (...) {}
                } else if (_stricmp(key.c_str(), "Zoom") == 0) {
                    try { currentItem.zoom = std::stof(val); } catch (...) {}
                } else if (_stricmp(key.c_str(), "Timestamp") == 0) {
                    try { currentItem.timestamp = std::stoull(val); } catch (...) {}
                }
            }
        }
    }
    FlushCurrent();
    file.close();

    // Deduplicate in case of duplicate entries
    std::vector<SessionItem> deduped;
    for (const auto& item : m_items) {
        if (item.filePath.empty()) continue;
        if (item.filePath.length() <= 3 && item.filePath.find(L'\\') == std::wstring::npos) continue;

        bool alreadyExists = false;
        for (const auto& d : deduped) {
            if (_wcsicmp(NormalizePath(d.filePath).c_str(), NormalizePath(item.filePath).c_str()) == 0) {
                alreadyExists = true;
                break;
            }
        }
        if (!alreadyExists) {
            deduped.push_back(item);
        }
    }
    m_items = std::move(deduped);

    // Sort LRU (newest timestamp first)
    std::sort(m_items.begin(), m_items.end(), [](const SessionItem& a, const SessionItem& b) {
        return a.timestamp > b.timestamp;
    });

    if (m_items.size() > MAX_HISTORY_ITEMS) {
        m_items.resize(MAX_HISTORY_ITEMS);
    }
}

void SessionHistory::Save() {
    std::wstring iniPath = GetIniPath();

    std::ofstream file(iniPath.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;

    file << "# FeathrPDF Configuration and Session History\n";
    file << "[Settings]\n";
    file << "DarkMode = " << (m_isDarkMode ? 1 : 0) << "\n\n";

    for (const auto& item : m_items) {
        if (item.filePath.empty()) continue;
        std::string utf8Path = WStringToUtf8(item.filePath);
        file << "[" << utf8Path << "]\n";
        file << "Path = \"" << utf8Path << "\"\n";
        file << "LastPage = " << item.lastPage << "\n";
        file << "ScrollY = " << item.scrollY << "\n";
        file << "Zoom = " << std::fixed << std::setprecision(4) << item.zoom << "\n";
        file << "Timestamp = " << item.timestamp << "\n\n";
    }

    file.close();
}

bool SessionHistory::GetSession(const std::wstring& filePath, SessionItem& outItem) const {
    std::wstring norm = NormalizePath(filePath);
    if (norm.empty()) return false;

    for (const auto& item : m_items) {
        if (_wcsicmp(NormalizePath(item.filePath).c_str(), norm.c_str()) == 0) {
            outItem = item;
            return true;
        }
    }
    return false;
}

void SessionHistory::SaveSession(const std::wstring& filePath, int lastPage, int scrollY, float zoom) {
    std::wstring norm = NormalizePath(filePath);
    if (norm.empty()) return;

    // Discard invalid paths or truncated legacy drive letters like "C"
    if (norm.length() <= 3 && norm.find(L'\\') == std::wstring::npos) {
        return;
    }

    // Verify file actually exists on disk
    if (GetFileAttributesW(norm.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    uint64_t nowSec = (uint64_t)time(nullptr);

    // 1. Deduplication: Remove any existing record for this file (case-insensitive)
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const SessionItem& item) {
        return _wcsicmp(NormalizePath(item.filePath).c_str(), norm.c_str()) == 0;
    }), m_items.end());

    // 2. Insert updated record at position 0 (top of recent list)
    SessionItem newItem;
    newItem.filePath = norm;
    newItem.lastPage = (lastPage < 1) ? 1 : lastPage;
    newItem.scrollY = scrollY;
    newItem.zoom = (zoom < 0.25f || zoom > 6.0f) ? 1.0f : zoom;
    newItem.timestamp = nowSec;

    m_items.insert(m_items.begin(), newItem);

    // 3. Limit to MAX_HISTORY_ITEMS (100)
    if (m_items.size() > MAX_HISTORY_ITEMS) {
        m_items.resize(MAX_HISTORY_ITEMS);
    }

    Save();
}

void SessionHistory::RemoveSession(const std::wstring& filePath) {
    std::wstring norm = NormalizePath(filePath);
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const SessionItem& item) {
        return _wcsicmp(NormalizePath(item.filePath).c_str(), norm.c_str()) == 0;
    }), m_items.end());
    Save();
}

void SessionHistory::ClearAll() {
    m_items.clear();
    Save();
}
