#pragma once

#include <windows.h>

class CustomColorPickerDialog {
public:
    // Shows the modal professional color picker dialog.
    // Returns true if user clicked OK, false if cancelled.
    static bool Show(HWND hWndParent, COLORREF currentColor, COLORREF& outColor);
};
