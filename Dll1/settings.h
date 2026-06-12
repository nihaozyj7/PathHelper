#pragma once

struct Settings;
void EnsureSettingsFile();
void LoadSettings(Settings &s);
bool IsDarkTheme();
