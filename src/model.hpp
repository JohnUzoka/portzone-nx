#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

namespace pz
{

// Where the game came from and which consoles the port runs on ("switch", "switch2").
struct Platform
{
    std::string original;
    std::vector<std::string> runsOn;
    bool present = false; // given in the file; otherwise ports.json's (or the defaults) apply
};

// ports.json entry
struct IndexEntry
{
    std::string id, url, title, author, description;
    std::string icon, banner; // https URLs, used when portzone.json has none
    std::string manifest = "portzone.json"; // the port's portzone.json, as a path in its repository
    Platform platform;
};

// portzone.json, see schema/portzone.schema.json in the store repository
struct FileSpec
{
    std::string path;  // relative to GameData::dir; a trailing / is a folder
    std::string match; // with a folder: files it must hold
    std::vector<std::string> markers; // with a folder: paths inside it that prove it is the right one
    std::string note;
    std::optional<uint64_t> size;
    std::vector<std::string> sha256;

    bool isFolder() const { return !path.empty() && path.back() == '/'; }
};

struct Option
{
    std::string label, url;
    std::vector<FileSpec> files;
};

struct Input
{
    std::string id, label;
    bool required = true;
    std::vector<Option> options;
};

struct Prepare
{
    std::string where; // "switch" or "pc"
    std::string summary, toolName, toolUrl;
    std::vector<std::string> done;
};

struct Generated
{
    std::string path, note;
};

struct GameData
{
    std::string dir;
    std::vector<Input> inputs;
    std::optional<Prepare> prepare;
    std::vector<Generated> generated;
};

// One thing to do with a release file: unzip it into a folder, or copy it into one.
struct InstallStep
{
    enum class Kind
    {
        Unzip,
        Copy,
    };
    std::string asset; // case-insensitive end of the release file's name
    Kind kind = Kind::Copy;
    std::string dest;  // sdmc:/ folder
    std::string from;  // unzip: folder in the zip to take, ending in /
    std::string name;  // copy: file name in dest; empty keeps the release file's
};

// A port's portzone.json
struct Manifest
{
    std::string description;
    std::string icon, banner; // paths in the port's repository
    Platform platform;
    bool prerelease = false;
    std::string binary;
    std::vector<InstallStep> installSteps; // empty: nothing to download (a PC tool makes the game)
    bool fullMemory = true;
    std::vector<std::string> launchNotes;
    std::optional<GameData> gameData;
    std::vector<std::string> steps; // instructions for the player
};

// Throw nlohmann::json::exception or std::runtime_error on a malformed file.
Manifest parseManifest(const nlohmann::json& json);
std::vector<IndexEntry> parseIndex(const nlohmann::json& json);

} // namespace pz
