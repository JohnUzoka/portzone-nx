#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace pz
{

namespace fs = std::filesystem;

struct Config
{
    // ports.json: an http(s) URL or a local file
    std::string indexUrl = "https://raw.githubusercontent.com/JohnUzoka/portzone-nx/main/store/ports.json";
    // The SD card: sdmc:/ on the Switch, a folder standing in for it on a PC
    fs::path sdRoot;
    // Optional: <id>/portzone.json files read before each port's repository
    fs::path draftsDir;
    // The console PortZone installs for: "switch" or "switch2" (a port's Switch 2 build). Set by the player in About
    // (PortZone can't tell a Switch 2 from a Switch); a PC build's --console wins over that setting.
    std::string console = "switch";
    bool consoleFromArgs = false;
    // Where PortZone's own releases are, for updating itself.
    std::string updateRepo = "JohnUzoka/portzone-nx";
    // PortZone's own .nro, replaced by an update: argv[0] on the Switch, --self on a PC. Empty: cannot update.
    fs::path selfPath;
};

Config& config();

// Reads --index, --sd, --drafts, --console, --update-repo and --self. Returns an error message, or "" when the
// arguments are usable. On the Switch, argv[0] is the .nro's path and other arguments are ignored.
std::string parseArgs(int argc, char* argv[]);

// "sdmc:/switch/x" -> the real path on this machine.
fs::path sdPath(const std::string& sdmcPath);

// "sdmc:/switch/x" -> "switch/x", as players see the SD card.
std::string sdShown(const std::string& sdmcPath);

// Where the app keeps its cache and install records: <sd>/config/portzone.
fs::path dataDir();

// Lowercases ASCII letters.
std::string lower(std::string text);

bool endsWithNoCase(const std::string& text, const std::string& suffix);

std::string formatSize(uint64_t bytes);

} // namespace pz
