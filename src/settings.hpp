#pragma once

#include <set>
#include <string>

namespace pz
{

// <sd>/config/portzone/settings.json
struct Settings
{
    bool lightTheme = false;
    bool gridView   = false; // Browse and Installed as icon tiles instead of cards
    bool checkUpdates = true; // look for a new PortZone at start
    std::set<std::string> testBuilds; // ports whose prereleases the player wants
    int sortOrder = 0;                // Browse and Installed: 0 name A-Z, 1 name Z-A, 2 by system
    std::string console;              // "switch" or "switch2": which build to install; "" until the player picks
};

Settings& settings();
void loadSettings();
void saveSettings();

} // namespace pz
