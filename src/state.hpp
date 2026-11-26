#pragma once

#include <optional>
#include <string>

#include "catalog.hpp"
#include "files.hpp"
#include "installer.hpp"

namespace pz
{

// What is on the SD card for one port right now.
struct PortState
{
    bool installed = false;              // the .nro is there (or, for a manual port, PortZone installed it)
    std::optional<InstallRecord> record; // what PortZone installed
    std::optional<DataCheck> data;       // the player's files, when the port lists them
};

PortState portState(const Port& port);

// One line for lists: "Ready to play", "Installed · files missing", ...
std::string stateSummary(const Port& port, const PortState& state);

// The same, short enough for a grid tile: "Ready", "Files missing", ...
std::string stateShort(const Port& port, const PortState& state);

// Whether stateSummary means everything is set.
bool stateGood(const Port& port, const PortState& state);

// "Switch", "Switch 2", "Switch and Switch 2".
std::string consolesLabel(const std::vector<std::string>& consoles);

} // namespace pz
