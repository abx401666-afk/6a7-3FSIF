#pragma once
#include <string>
#include <vector>

struct ConfigInfo {
    std::string name;
    double ageSeconds = 0.0;
};

void        Config_Init();
void        Config_SaveSession(bool force = false);
std::vector<ConfigInfo> Config_List();
bool        Config_Exists(const std::string& name);
bool        Config_Save(const std::string& name);
bool        Config_Load(const std::string& name);
bool        Config_Delete(const std::string& name);
bool        Config_Rename(const std::string& from, const std::string& to);
void        Config_ResetDefaults();
std::string Config_Sanitize(const std::string& name);

const std::string& Config_Active();
bool        Config_Dirty();
const std::string& Config_Autoload();
void        Config_SetAutoload(const std::string& name);
void        Config_OpenFolder();
std::string Config_LastError();
