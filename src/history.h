#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

struct SessionItem {
    std::wstring filePath;
    int lastPage = 1;       // 1-based page number
    int scrollY = 0;        // Vertical scroll offset
    float zoom = 1.0f;      // Current zoom factor
    uint64_t timestamp = 0; // Unix timestamp in seconds
};

class SessionHistory {
public:
    static SessionHistory& Instance() {
        static SessionHistory inst;
        return inst;
    }

    SessionHistory();
    ~SessionHistory();

    void Load();
    void Save();

    bool GetSession(const std::wstring& filePath, SessionItem& outItem) const;
    void SaveSession(const std::wstring& filePath, int lastPage, int scrollY, float zoom);
    void RemoveSession(const std::wstring& filePath);
    void ClearAll();

    bool IsDarkMode() const { return m_isDarkMode; }
    void SetDarkMode(bool dark) { m_isDarkMode = dark; Save(); }

    const std::vector<SessionItem>& GetRecentList() const { return m_items; }

private:
    std::wstring GetIniPath() const;
    std::vector<SessionItem> m_items;
    bool m_isDarkMode = false;
    static const size_t MAX_HISTORY_ITEMS = 100;
};
