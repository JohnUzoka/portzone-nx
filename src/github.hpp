#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pz::github
{

struct Asset
{
    std::string name, url, sha256; // sha256 from GitHub's digest; empty for old uploads
    uint64_t size = 0;
};

struct Release
{
    std::string tag, name, published;
    std::string notes; // the release's description, Markdown
    bool prerelease = false;
    std::vector<Asset> assets;
};

struct Releases
{
    std::vector<Release> releases; // newest first
    std::string error;
};

// One GitHub API call per repository per session; later calls reuse it unless `fresh`.
Releases fetchReleases(const std::string& repoUrl, bool fresh = false);

struct Picked
{
    Release release;
    std::vector<Asset> assets; // one per suffix, in the same order
};

// The newest release that has an asset ending in each of `suffixes` (ignoring case).
std::optional<Picked> pickRelease(const std::vector<Release>& releases, const std::vector<std::string>& suffixes,
    bool prerelease);

// The newest release; with `prerelease` false, skipping prereleases when there is a stable one.
const Release* latest(const std::vector<Release>& releases, bool prerelease);

struct Readme
{
    std::string text, error;
};

// README.md from raw.githubusercontent.com (free); other names through the API.
Readme fetchReadme(const std::string& repoUrl);

} // namespace pz::github
