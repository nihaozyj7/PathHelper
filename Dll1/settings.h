#pragma once

struct Settings;
void EnsureSettingsFile();
void LoadSettings(Settings &s);
bool IsDarkTheme();

// 配置文件有变化时重新读取（按最后写入时间判断），返回是否真的变了
bool ReloadSettingsIfChanged(bool force);
// 保存搜索结果面板的尺寸（0 表示恢复自动）
bool SaveSearchPanelSize(int width, int height);
