#include "update.hpp"

#include <borealis.hpp>
#include <cctype>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "config.hpp"

#ifndef PORTZONE_VERSION
#define PORTZONE_VERSION "dev"
#endif

namespace pz::update
{

namespace
{

    struct Version
    {
        std::vector<long> numbers;
        bool prerelease = false; // "-test.1" and the like
    };

    Version parse(std::string text)
    {
        Version version;
        if (!text.empty() && (text[0] == 'v' || text[0] == 'V'))
            text.erase(0, 1);
        const auto dash = text.find('-');
        version.prerelease = dash != std::string::npos;
        const std::string core = text.substr(0, dash);
        size_t start           = 0;
        while (start <= core.size())
        {
            const size_t dot = std::min(core.find('.', start), core.size());
            long number      = 0;
            for (size_t i = start; i < dot && std::isdigit(static_cast<unsigned char>(core[i])); i++)
                number = number * 10 + (core[i] - '0');
            version.numbers.push_back(number);
            start = dot + 1;
        }
        return version;
    }

} // namespace

int compareVersions(const std::string& a, const std::string& b)
{
    const Version va = parse(a), vb = parse(b);
    for (size_t i = 0; i < std::max(va.numbers.size(), vb.numbers.size()); i++)
    {
        const long x = i < va.numbers.size() ? va.numbers[i] : 0;
        const long y = i < vb.numbers.size() ? vb.numbers[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return va.prerelease == vb.prerelease ? 0 : (va.prerelease ? -1 : 1);
}

Status check()
{
    Status status;
    status.checked     = true;
    const auto results = github::fetchReleases("https://github.com/" + config().updateRepo, true);
    if (!results.error.empty())
    {
        status.error = results.error;
        return status;
    }
    // The newest stable release with PortZone's .nro: portzone.nro, or the release's only .nro.
    for (const auto& release : results.releases)
    {
        if (release.prerelease)
            continue;
        const github::Asset* nro = nullptr;
        size_t nros              = 0;
        for (const auto& asset : release.assets)
            if (endsWithNoCase(asset.name, ".nro"))
            {
                nros++;
                if (!nro || lower(asset.name) == "portzone.nro")
                    nro = &asset;
            }
        if (!nro || (nros > 1 && lower(nro->name) != "portzone.nro"))
            continue;
        status.tag       = release.tag;
        status.notes     = release.notes;
        status.asset     = *nro;
        status.available = compareVersions(release.tag, PORTZONE_VERSION) > 0;
        return status;
    }
    status.error = "No PortZone release has a .nro yet.";
    return status;
}

Status& current()
{
    static Status status;
    return status;
}

bool canInstall() { return !config().selfPath.empty(); }

void restart()
{
#ifdef __SWITCH__
    const std::string self = config().selfPath.string();
    envSetNextLoad(self.c_str(), self.c_str());
#endif
    brls::Application::quit();
}

} // namespace pz::update
