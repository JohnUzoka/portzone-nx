#include "settings.hpp"

#include <fstream>

#include <json.hpp>

#include "config.hpp"

namespace pz
{

namespace
{
    fs::path settingsPath() { return dataDir() / "settings.json"; }
} // namespace

Settings& settings()
{
    static Settings instance;
    return instance;
}

void loadSettings()
{
    // Before last_version existed, or before any setting was saved: an earlier PortZone left its cache.
    std::error_code ec;
    if (fs::exists(dataDir() / "cache", ec))
        settings().lastVersion = "0";
    std::ifstream file(settingsPath());
    if (!file)
        return;
    try
    {
        const auto json       = nlohmann::json::parse(file);
        settings().lightTheme = json.value("theme", "dark") == "light";
        settings().gridView   = json.value("view", "cards") == "grid";
        settings().checkUpdates = json.value("check_updates", true);
        settings().testBuilds   = json.value("test_builds", std::set<std::string>());
        const std::string sort  = json.value("sort", "name");
        settings().sortOrder    = sort == "name-desc" ? 1 : (sort == "system" ? 2 : 0);
        const std::string console = json.value("console", "");
        settings().console      = console == "switch" || console == "switch2" ? console : "";
        settings().language     = json.value("language", "");
        settings().lastVersion  = json.value("last_version", std::string("0"));
    }
    catch (const std::exception&)
    {
        // A damaged file: keep the defaults; the next save replaces it.
    }
}

void saveSettings()
{
    const nlohmann::json json = {
        { "theme", settings().lightTheme ? "light" : "dark" },
        { "view", settings().gridView ? "grid" : "cards" },
        { "check_updates", settings().checkUpdates },
        { "test_builds", settings().testBuilds },
        { "sort", settings().sortOrder == 1 ? "name-desc" : (settings().sortOrder == 2 ? "system" : "name") },
        { "console", settings().console },
        { "language", settings().language },
        { "last_version", settings().lastVersion },
    };
    std::ofstream(settingsPath()) << json.dump(2) << "\n";
}

} // namespace pz
