#pragma once

#include <windows.h>
#include "app.h"

class ToolSettingsPopup {
public:
    static void Show(HWND hWndParent, ToolMode tool, int anchorX, int anchorY);
    static void Close();
    static bool IsOpen();
};
