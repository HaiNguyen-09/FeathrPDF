#pragma once

#include <windows.h>
#include <commctrl.h>

#define IDC_RIBBON_BTN_FILE         2000
#define IDC_RIBBON_BTN_OPEN         2001
#define IDC_RIBBON_BTN_SAVE         2002
#define IDC_RIBBON_BTN_SAVEAS       2003
#define IDC_RIBBON_BTN_PRINT        2004
#define IDC_RIBBON_BTN_NEWPAGE      2005
#define IDC_RIBBON_BTN_DELPAGE      2006
#define IDC_RIBBON_BTN_EXPORTJPG    2007
#define IDC_RIBBON_BTN_TOOLPAN      2008
#define IDC_RIBBON_BTN_TOOLHL       2009
#define IDC_RIBBON_BTN_TOOLPEN      2010
#define IDC_RIBBON_SLIDER_ZOOM      2011
#define IDC_RIBBON_BTN_FITWIDTH     2012
#define IDC_RIBBON_EDIT_GOTOPAGE    2013
#define IDC_RIBBON_BTN_TOGGLESIDE   2014
#define IDC_RIBBON_BTN_COLLAPSE     2015
#define IDC_RIBBON_BTN_UNDO         2016
#define IDC_RIBBON_BTN_REDO         2017
#define IDC_RIBBON_BTN_APPENDPDF    2018
#define IDC_RIBBON_BTN_EXPORTPDF    2019
#define IDC_RIBBON_BTN_RECENT       2020
#define IDC_RIBBON_BTN_TOOLERASER   2021
#define IDC_RIBBON_BTN_TOOLHL_DROP  2022
#define IDC_RIBBON_BTN_TOOLPEN_DROP 2023

class RibbonBar {
public:
    static bool RegisterClass(HINSTANCE hInstance);
    static HWND Create(HWND hWndParent, HINSTANCE hInstance);
    static void UpdateUI(HWND hRibbon);
    static void SetZoomSlider(HWND hRibbon, int zoomPercent);
    static void SetPageInfo(HWND hRibbon, int curPage, int totalPages, int selectedCount);
    static int GetHeight(bool isCollapsed);
};
