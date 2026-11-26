#include "github.hpp"

#include <ctime>
#include <map>
#include <mutex>

#include <json.hpp>

#include "config.hpp"
#include "http.hpp"

namespace pz::github
{

namespace
{

    std::mutex cacheMutex;
    std::map<std::string, Releases> cache;

    std::string repoPath(const std::string& url) { return url.substr(std::string("https://github.com/").size()); }

    std::string apiError(const http::Response& response)
    {
        if (!response.error.empty())
            return response.error;
        const auto remaining = response.headers.find("x-ratelimit-remaining");
        if ((response.status == 403 || response.status == 429) && remaining != response.headers.end() &&
            remaining->second == "0")
        {
            std::string when;
            const auto reset = response.headers.find("x-ratelimit-reset");
            if (reset != response.headers.end())
            {
                const std::time_t at = std::time_t(std::stoll(reset->second));
                char text[16];
                std::strftime(text, sizeof(text), "%H:%M", std::localtime(&at));
                when = std::string(" Try again after ") + text + ".";
            }
            return "GitHub allows 60 requests an hour from your network, and they are used up." + when;
        }
        if (response.status == 404)
            return "The repository or its releases were not found.";
        return "GitHub answered HTTP " + std::to_string(response.status) + ".";
    }

    const std::vector<std::string> API_HEADERS = { "Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28" };

} // namespace

Releases fetchReleases(const std::string& repoUrl, bool fresh)
{
    if (!fresh)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto hit = cache.find(repoUrl);
        if (hit != cache.end())
            return hit->second;
    }

    Releases result;
    const auto response = http::get("https://api.github.com/repos/" + repoPath(repoUrl) + "/releases?per_page=20", API_HEADERS);
    if (!response.ok())
    {
        result.error = apiError(response);
        return result;
    }
    try
    {
        for (const auto& item : nlohmann::json::parse(response.body))
        {
            if (item.value("draft", false))
                continue;
            Release release;
            release.tag        = item.value("tag_name", "");
            release.name       = item.value("name", nlohmann::json()).is_string() ? item.value("name", "") : "";
            release.published  = item.value("published_at", nlohmann::json()).is_string() ? item.value("published_at", "") : "";
            release.notes      = item.value("body", nlohmann::json()).is_string() ? item.value("body", "") : "";
            release.prerelease = item.value("prerelease", false);
            for (const auto& a : item.at("assets"))
            {
                Asset asset;
                asset.name                 = a.value("name", "");
                asset.url                  = a.value("browser_download_url", "");
                asset.size                 = a.value("size", uint64_t(0));
                const std::string digest   = a.value("digest", nlohmann::json()).is_string() ? a.value("digest", "") : "";
                if (digest.rfind("sha256:", 0) == 0)
                    asset.sha256 = digest.substr(7);
                release.assets.push_back(std::move(asset));
            }
            result.releases.push_back(std::move(release));
        }
    }
    catch (const std::exception& e)
    {
        result.error = std::string("Unexpected answer from GitHub: ") + e.what();
        return result;
    }

    std::lock_guard<std::mutex> lock(cacheMutex);
    cache[repoUrl] = result;
    return result;
}

std::optional<Picked> pickRelease(const std::vector<Release>& releases, const std::vector<std::string>& suffixes,
    bool prerelease)
{
    for (const auto& release : releases)
    {
        if (release.prerelease && !prerelease)
            continue;
        Picked picked { release, {} };
        for (const auto& suffix : suffixes)
            for (const auto& asset : release.assets)
                if (endsWithNoCase(asset.name, suffix))
                {
                    picked.assets.push_back(asset);
                    break;
                }
        if (picked.assets.size() == suffixes.size())
            return picked;
    }
    return std::nullopt;
}

const Release* latest(const std::vector<Release>& releases, bool prerelease)
{
    if (!prerelease)
        for (const auto& release : releases)
            if (!release.prerelease)
                return &release;
    return releases.empty() ? nullptr : &releases.front();
}

Readme fetchReadme(const std::string& repoUrl)
{
    Readme readme;
    auto response = http::get("https://raw.githubusercontent.com/" + repoPath(repoUrl) + "/HEAD/README.md");
    if (!response.ok() && response.error.empty() && response.status == 404)
        response = http::get("https://api.github.com/repos/" + repoPath(repoUrl) + "/readme",
            { "Accept: application/vnd.github.raw", "X-GitHub-Api-Version: 2022-11-28" });
    if (response.ok())
        readme.text = response.body;
    else
        readme.error = apiError(response);
    return readme;
}

} // namespace pz::github
