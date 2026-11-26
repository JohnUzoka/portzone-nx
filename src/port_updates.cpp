#include "port_updates.hpp"

namespace pz::portupdates
{

std::string newerTag(const Port& port, const InstallRecord& record, const std::vector<github::Release>& releases,
    bool prerelease)
{
    if (port.manifest)
    {
        if (port.manifest->installSteps.empty())
            return "";
        std::vector<std::string> suffixes;
        for (const auto& step : port.manifest->installSteps)
            suffixes.push_back(step.asset);
        const auto picked = github::pickRelease(releases, suffixes, prerelease || port.manifest->prerelease);
        return picked && picked->release.tag != record.tag ? picked->release.tag : "";
    }
    // A manual install: the newest release, when it still has the file the player picked.
    const github::Release* newest = github::latest(releases, prerelease);
    if (!newest || newest->tag == record.tag)
        return "";
    for (const auto& asset : newest->assets)
        if (asset.name == record.asset)
            return newest->tag;
    return "";
}

std::map<std::string, std::string> check(const std::vector<Port>& ports, const std::set<std::string>& testBuilds)
{
    std::map<std::string, std::string> out;
    for (const Port& port : ports)
    {
        const auto record = loadRecord(port.info.id);
        if (!record || !port.runsHere())
            continue;
        const github::Releases releases = github::fetchReleases(port.info.url);
        if (!releases.error.empty())
            continue;
        const std::string tag = newerTag(port, *record, releases.releases, testBuilds.count(port.info.id) > 0);
        if (!tag.empty())
            out[port.info.id] = tag;
    }
    return out;
}

std::map<std::string, std::string>& found()
{
    static std::map<std::string, std::string> instance;
    return instance;
}

std::string pending(const std::string& id)
{
    const auto it = found().find(id);
    if (it == found().end())
        return "";
    const auto record = loadRecord(id);
    return record && record->tag != it->second ? it->second : "";
}

size_t count()
{
    size_t n = 0;
    for (const auto& entry : found())
        n += !pending(entry.first).empty();
    return n;
}

} // namespace pz::portupdates
