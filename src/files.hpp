#pragma once

#include <optional>
#include <string>
#include <vector>

#include "config.hpp"
#include "model.hpp"

namespace pz
{

// Finds `relative` under `base`, ignoring case (FAT/exFAT do; a PC's disk may not).
// `*` and `?` work in the last part. Returns the real path, or nothing.
std::optional<fs::path> findPath(const fs::path& base, const std::string& relative);

// Files in `folder` (not below it) whose names match `pattern`, ignoring case.
size_t countMatches(const fs::path& folder, const std::string& pattern);

bool globMatch(const std::string& pattern, const std::string& name);

// Files up to this size are checked against portzone.json's sha256; bigger ones would take too long on the Switch.
constexpr uint64_t HASH_LIMIT = 64ull << 20;

// Lowercase hex.
std::string sha256Of(const fs::path& path);

// Moves `from` to `to`, replacing a file there and making its folders; when a rename can't (another drive on a PC),
// copies and deletes. "" or what went wrong. std::filesystem::copy_file can't copy on the Switch: don't use it.
std::string moveFile(const fs::path& from, const fs::path& to);

struct FileCheck
{
    const FileSpec* spec = nullptr;
    bool found           = false;
    std::string detail; // "41 files", "wrong size", the name it matched
};

struct OptionCheck
{
    const Option* option = nullptr;
    std::vector<FileCheck> files;
    bool met = false;
};

struct InputCheck
{
    const Input* input = nullptr;
    std::vector<OptionCheck> options;
    int metOption = -1; // the first option whose files are all present
    bool met      = false;
};

struct DataCheck
{
    std::vector<InputCheck> inputs;
    bool converted = false; // prepare.done files are all present
    bool ready     = false; // every required input is met
    std::vector<std::string> missing; // labels of required inputs that are not
};

DataCheck checkGameData(const GameData& data);

// Whether `relative` under the game folder exists: a file, or a non-empty folder for a trailing /.
bool present(const GameData& data, const std::string& relative);

} // namespace pz
