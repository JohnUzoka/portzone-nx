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
};

Settings& settings();
void loadSettings();
void saveSettings();

} // namespace pz
