#include "catalog.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <thread>

#include <borealis/core/assets.hpp>

#include "config.hpp"
#include "http.hpp"

namespace pz
{

using nlohmann::json;

namespace
{

    bool readFile(const fs::path& path, std::string& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;
        std::ostringstream text;
        text << file.rdbuf();
        out = text.str();
        return true;
    }

    void writeFile(const fs::path& path, const std::string& text)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream(path, std::ios::binary) << text;
    }

    fs::path cacheDir() { return dataDir() / "cache"; }

    std::string repoPath(const std::string& url) { return url.substr(std::string("https://github.com/").size()); }

    bool isUrl(const std::string& s) { return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0; }

    // A store draft (store/drafts/<id>/portzone.json, the file a port's porter has not added to their repository yet)
    // from beside the port list in use: a URL, a folder, or PortZone's built-in copy.
    bool readStoreDraft(const std::string& draftsBase, const std::string& id, std::string& text, std::string& source)
    {
        if (draftsBase.empty())
            return false;
        const std::string where = draftsBase + "/" + id + "/portzone.json";
        if (isUrl(where))
        {
            const auto response = http::get(where);
            if (!response.ok())
                return false;
            text = response.body;
        }
        else if (!readFile(where, text))
            return false;
        source = where + " (store draft)";
        return true;
    }

    // portzone.json for one port: a --drafts draft, then the repository, then the store's draft, then the cache.
    void loadManifest(Port& port, const std::string& draftsBase)
    {
        std::string text;
        const fs::path cached = cacheDir() / "manifests" / (port.info.id + ".json");
        const fs::path draft  = config().draftsDir.empty() ? fs::path() : config().draftsDir / port.info.id / "portzone.json";

        if (!draft.empty() && readFile(draft, text))
            port.manifestSource = draft.string();
        else
        {
            const std::string url = "https://raw.githubusercontent.com/" + repoPath(port.info.url) + "/HEAD/portzone.json";
            const auto response   = http::get(url);
            if (response.ok())
            {
                text                = response.body;
                port.manifestSource = url;
                writeFile(cached, text);
            }
            else if (response.error.empty() && response.status == 404)
            {
                if (readStoreDraft(draftsBase, port.info.id, text, port.manifestSource))
                    writeFile(cached, text);
                else
                {
                    std::error_code ec;
                    fs::remove(cached, ec);
                    return; // no portzone.json: manual install
                }
            }
            else if (readFile(cached, text))
                port.manifestSource = cached.string() + " (offline)";
            else
                return;
        }

        try
        {
            port.manifest = parseManifest(json::parse(text));
        }
        catch (const std::exception& e)
        {
            port.manifestError = std::string("portzone.json: ") + e.what();
        }
    }

} // namespace

Section Port::section() const
{
    if (!manifest)
        return Section::Manual;
    if (manifest->gameData && manifest->gameData->prepare && manifest->gameData->prepare->where == "pc")
        return Section::BuildOnPc;
    return Section::EasySetup;
}

const std::string& Port::description() const
{
    return manifest && !manifest->description.empty() ? manifest->description : info.description;
}

std::string Port::iconUrl() const
{
    const std::string repo = repoPath(info.url);
    if (manifest && !manifest->icon.empty())
        return "https://raw.githubusercontent.com/" + repo + "/HEAD/" + manifest->icon;
    if (!info.icon.empty())
        return info.icon;
    return "https://github.com/" + repo.substr(0, repo.find('/')) + ".png?size=256";
}

std::string Port::bannerUrl() const
{
    if (manifest && !manifest->banner.empty())
        return "https://raw.githubusercontent.com/" + repoPath(info.url) + "/HEAD/" + manifest->banner;
    return info.banner;
}

const std::string& Port::originalPlatform() const
{
    return manifest && !manifest->platform.original.empty() ? manifest->platform.original : info.platform.original;
}

std::vector<std::string> Port::runsOn() const
{
    if (manifest && !manifest->platform.runsOn.empty())
        return manifest->platform.runsOn;
    if (!info.platform.runsOn.empty())
        return info.platform.runsOn;
    return { "switch" };
}

bool Port::runsHere() const
{
    const auto consoles = runsOn();
    return std::find(consoles.begin(), consoles.end(), config().console) != consoles.end();
}

std::string Port::defaultFolder() const
{
    const std::string repo = repoPath(info.url);
    return "sdmc:/switch/" + repo.substr(repo.find('/') + 1);
}

Catalog& Catalog::get()
{
    static Catalog instance;
    return instance;
}

const Port* Catalog::find(const std::string& id) const
{
    for (const auto& port : ports)
        if (port.info.id == id)
            return &port;
    return nullptr;
}

Catalog::Loaded Catalog::fetch()
{
    const std::string& url = config().indexUrl;
    const fs::path cached  = cacheDir() / "ports.json";
    Loaded result;
    std::string text;

    std::string draftsBase; // store/drafts beside the port list used
    if (isUrl(url))
    {
        const auto response = http::get(url);
        if (response.ok())
        {
            text = response.body;
            writeFile(cached, text);
        }
        else if (readFile(cached, text))
            result.source = Source::Cache;
        else if (readFile(BRLS_ASSET("ports.json"), text))
            result.source = Source::BuiltIn;
        else
        {
            result.error = "Could not load the port list from " + url + ": " +
                (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
            return result;
        }
        draftsBase = result.source == Source::BuiltIn ? std::string(BRLS_ASSET("drafts")) : url.substr(0, url.rfind('/')) + "/drafts";
    }
    else if (!readFile(url, text))
    {
        result.error = "Could not read " + url;
        return result;
    }
    else
        draftsBase = (fs::absolute(url).parent_path() / "drafts").string();

    std::vector<IndexEntry> entries;
    try
    {
        entries = parseIndex(json::parse(text));
    }
    catch (const std::exception& e)
    {
        result.error = std::string("The port list is not valid: ") + e.what();
        return result;
    }

    // Two at a time: a homebrew started from the Album gets only two network sessions.
    std::vector<Port>& loaded = result.ports;
    loaded.resize(entries.size());
    for (size_t i = 0; i < entries.size(); i++)
        loaded[i].info = entries[i];
    std::atomic<size_t> next { 0 };
    std::vector<std::thread> workers;
    for (int w = 0; w < 2; w++)
        workers.emplace_back([&] {
            for (size_t i = next++; i < loaded.size(); i = next++)
                loadManifest(loaded[i], draftsBase);
        });
    for (auto& worker : workers)
        worker.join();

    std::sort(loaded.begin(), loaded.end(), [](const Port& a, const Port& b) { return lower(a.info.title) < lower(b.info.title); });
    return result;
}

void Catalog::apply(Loaded loaded)
{
    ports  = std::move(loaded.ports);
    source = loaded.source;
}

} // namespace pz
