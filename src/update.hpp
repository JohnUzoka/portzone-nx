#pragma once

#include <string>

#include "github.hpp"

namespace pz::update
{

// What GitHub says about PortZone's own releases (config().updateRepo).
struct Status
{
    bool checked   = false;
    bool available = false; // a newer stable release with a .nro
    bool dismissed = false; // "Later" in Browse, for this session
    std::string error;      // the check failed
    std::string tag;        // the newest release
    std::string notes;      // its description, Markdown
    github::Asset asset;    // its .nro
};

// Blocking: one GitHub API call.
Status check();

// UI thread only.
Status& current();

// < 0, 0, > 0 as `a` is older than, the same as, or newer than `b`. "v1.2.10" > "1.2.9" > "1.2.9-test.1".
int compareVersions(const std::string& a, const std::string& b);

// Whether this copy can replace itself: the Switch build, started from an .nro on the SD card.
bool canInstall();

// Leaves PortZone and, on the Switch, has the homebrew loader start the (new) .nro again.
void restart();

} // namespace pz::update
