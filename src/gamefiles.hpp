#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "config.hpp"
#include "installer.hpp"
#include "matcher.hpp"

namespace pz::gamefiles
{

// Every file on the SD card that could be a game file, with paths relative to the SD root. Skips the system's and
// CFW's folders, PortZone's own data and the game folders of `ports` (files there are already in place). Blocking, any
// thread; stops early when `cancel` is set. `seen` counts the files looked at, for progress.
std::vector<matcher::Source> listSdCard(const std::vector<const Port*>& ports, const std::atomic<bool>& cancel,
    std::atomic<size_t>& seen);

// Moves the chosen matches' files (sources from listSdCard) into their ports' game folders, replacing files already
// there, then removes the folders it emptied. One at a time: nullptr while another move runs (job id "@gamefiles").
std::shared_ptr<Job> startMove(std::vector<matcher::Source> sources, std::vector<matcher::Match> chosen);

// Where a placed file goes on this machine.
fs::path destination(const matcher::Match& match, const matcher::Placement& file);

// Whether a folder at the SD card's root belongs to the system or the CFW (atmosphere, Nintendo, ...), ignoring case.
// PortZone never searches, lists or writes there.
bool isSystemFolder(const std::string& name);

} // namespace pz::gamefiles
