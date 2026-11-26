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
    };
    std::ofstream(settingsPath()) << json.dump(2) << "\n";
}

} // namespace pz
