#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "catalog.hpp"
#include "github.hpp"
#include "installer.hpp"

// New releases of the ports PortZone installed: many ports can't update themselves.
namespace pz::portupdates
{

// The tag PortZone would update `port` to (what its page offers as "Update to"), or "" when it has the newest. Ports a
// PC tool makes (no install steps) are never offered. `prerelease`: the port's test builds count.
std::string newerTag(const Port& port, const InstallRecord& record, const std::vector<github::Release>& releases,
    bool prerelease);

// Blocking, any thread: one GitHub API call per port PortZone installed. Port id -> newer tag. `testBuilds`: ports
// whose prereleases count.
std::map<std::string, std::string> check(const std::vector<Port>& ports, const std::set<std::string>& testBuilds);

// UI thread: what the last check found.
std::map<std::string, std::string>& found();

// UI thread: the newer tag for `id`, or "" once the port is on it (or there was none).
std::string pending(const std::string& id);

// UI thread: how many ports have a pending update.
size_t count();

} // namespace pz::portupdates
