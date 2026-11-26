#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "catalog.hpp"

namespace pz::matcher
{

// One file the player offers: dropped in a browser (path below what was dropped) or found on the SD card (path below
// the SD card's root). '/' separates folders.
struct Source
{
    std::string path;
    uint64_t size = 0;
};

// One file to put in place.
struct Placement
{
    size_t source = 0;               // index into the Source list
    std::string dest;                // relative to the port's game_data.dir, '/' separators
    std::optional<uint64_t> size;    // portzone.json's exact size, checked when the file arrives
    std::vector<std::string> sha256; // accepted hashes, lowercase; empty: not checked
};

// One input of one port, met by some of the sources.
struct Match
{
    std::string portId, portTitle;
    std::string gameDir; // game_data.dir, "sdmc:/..."
    std::string inputId, inputLabel, optionLabel;
    bool required   = true;
    bool alreadyMet = false; // the SD card already has this input
    std::vector<Placement> files;
    uint64_t bytes = 0;
    int score      = 0;
    int group      = -1;    // matches with the same group (>= 0) are alternatives: pick at most one
    bool selected  = false; // the suggested choice
};

struct Plan
{
    std::vector<Match> matches;
    size_t unused = 0; // sources no match uses
};

// Which inputs of `ports` (each with game_data) the sources meet, and where each file goes. `onlyInput` limits the
// search to one input id (a drop on that input). Reads the SD card to tell which inputs are already met.
//
// A file spec matches a source whose path ends in the spec's path (ignoring case; * and ? in the last part). A folder
// spec matches a folder holding all its `markers` (or, without markers, a file matching `match`); every file below it
// goes along (only files matching `match` when that is set). An option matches when all its files do below one common
// folder; leading folders the option's paths share may be missing (a spec's import/code.bin matches oot/code.bin).
Plan plan(const std::vector<Source>& sources, const std::vector<const Port*>& ports, const std::string& onlyInput = "");

// Files never worth moving: Thumbs.db, .DS_Store, desktop.ini, ._* and the like.
bool isJunk(const std::string& name);

} // namespace pz::matcher
