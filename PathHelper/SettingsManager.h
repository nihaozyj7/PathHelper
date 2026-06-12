#pragma once
#include <windows.h>

void LoadSettingsToUI(HWND hChkAutoToLatest, HWND hCboTheme,
                      HWND hEditHistoryWidth, HWND hEditHistoryFontSize,
                      HWND hEditPanelMargin, HWND hChkStripCommonPrefix,
                      HWND hEditHistoryDisplayMax, HWND hEditTimeFormat, HWND hHotKeyTimePaste,
                      HWND hChkAutoStartToTray);
void SaveSettingsFromUI(HWND hChkAutoToLatest, HWND hCboTheme,
                        HWND hEditHistoryWidth, HWND hEditHistoryFontSize,
                        HWND hEditPanelMargin, HWND hChkStripCommonPrefix,
                        HWND hEditHistoryDisplayMax, HWND hEditTimeFormat,
                        HWND hChkAutoStartToTray);
