#include "catalog.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include <borealis/core/assets.hpp>
#include <borealis/core/logger.hpp>

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

    // Where a port's portzone.json came from, as the About tab shows it.
    constexpr const char* FROM_REPOSITORY = "the repository (the store's copy)";
    constexpr const char* FROM_DRAFT      = "the store's draft";

    std::string ownManifestUrl(const Port& port)
    {
        return "https://raw.githubusercontent.com/" + repoPath(port.info.url) + "/HEAD/" + port.info.manifest;
    }

    // `json` (a portzone.json) into the port, or the reason it can't be used. On a Switch 2, its "switch2" keys
    // replace the top-level ones (a separate Switch 2 build); `manifestJson` keeps the file as it is.
    void setManifest(Port& port, const json& document, std::string source)
    {
        port.manifestJson   = document;
        port.manifestSource = std::move(source);
        try
        {
            json effective = document;
            if (config().console == "switch2" && document.is_object() && document.contains("switch2"))
                for (const auto& [key, value] : document.at("switch2").items())
                    effective[key] = value;
            port.manifest = parseManifest(effective);
        }
        catch (const std::exception& e)
        {
            port.manifest.reset();
            port.manifestError = std::string("portzone.json: ") + e.what();
        }
    }

    // `location` (a store file's URL, or a local path) for a store file saved as `name`: from GitHub (saved for
    // offline use), else the copy saved last time, else the one built into PortZone.
    bool readStoreFile(const std::string& location, const std::string& name, std::string& text, Catalog::Source& source,
        std::string& error)
    {
        if (!isUrl(location))
        {
            if (readFile(location, text))
                return true;
            error = "Could not read " + location;
            return false;
        }
        const fs::path cached = cacheDir() / name;
        const auto response   = http::get(location);
        if (response.ok())
        {
            text   = response.body;
            source = Catalog::Source::Network;
            writeFile(cached, text);
            return true;
        }
        if (readFile(cached, text))
        {
            source = Catalog::Source::Cache;
            return true;
        }
        if (readFile(std::string(BRLS_ASSET("")) + name, text))
        {
            source = Catalog::Source::BuiltIn;
            return true;
        }
        error = "Could not load " + location + ": " +
            (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error);
        return false;
    }

} // namespace

Section Port::section() const
{
    if (!manifest)
        return Section::Manual;
    if (manifest->gameData && manifest->gameData->prepare && manifest->gameData->prepare->where == "pc")
        return manifest->installSteps.empty() ? Section::BuildOnPc : Section::PrepareOnPc;
    return Section::EasySetup;
}

const char* sectionName(Section section)
{
    switch (section)
    {
        case Section::EasySetup: return "Easy Setup";
        case Section::PrepareOnPc: return "Prepare on PC";
        case Section::BuildOnPc: return "Build on PC";
        case Section::Manual: break;
    }
    return "Manual";
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
    Loaded result;
    std::string text;
    if (!readStoreFile(url, "ports.json", text, result.source, result.error))
        return result;

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

    // Every port's portzone.json in one file beside ports.json; without it every port is a manual install.
    std::map<std::string, std::pair<json, std::string>> manifests; // id -> (portzone.json, where it came from)
    std::string indexText, indexError;
    Source indexSource = result.source;
    const std::string indexLocation = isUrl(url) ? url.substr(0, url.rfind('/') + 1) + "manifests.json" :
                                                   (fs::absolute(url).parent_path() / "manifests.json").string();
    if (readStoreFile(indexLocation, "manifests.json", indexText, indexSource, indexError))
    {
        try
        {
            // Named: a temporary in the loop's range would be destroyed before the loop runs.
            const json index = json::parse(indexText);
            for (const auto& [id, entry] : index.at("manifests").items())
                manifests[id] = { entry.at("portzone"),
                    entry.value("source", "") == "store draft" ? FROM_DRAFT : FROM_REPOSITORY };
        }
        catch (const std::exception& e)
        {
            brls::Logger::error("manifests.json: {}", e.what());
        }
    }
    else
        brls::Logger::error("{}", indexError);

    std::vector<Port>& loaded = result.ports;
    loaded.resize(entries.size());
    for (size_t i = 0; i < entries.size(); i++)
    {
        Port& port = loaded[i];
        port.info  = entries[i];
        const fs::path draft =
            config().draftsDir.empty() ? fs::path() : config().draftsDir / port.info.id / "portzone.json";
        std::string draftText;
        if (!draft.empty() && readFile(draft, draftText))
        {
            try
            {
                setManifest(port, json::parse(draftText), draft.string());
            }
            catch (const std::exception& e)
            {
                port.manifestError = std::string("portzone.json: ") + e.what();
            }
        }
        else if (const auto found = manifests.find(port.info.id); found != manifests.end())
            setManifest(port, found->second.first, found->second.second);
    }

    std::sort(loaded.begin(), loaded.end(), [](const Port& a, const Port& b) { return lower(a.info.title) < lower(b.info.title); });
    return result;
}

std::optional<Port> Catalog::fresher(const Port& port)
{
    if (!config().draftsDir.empty() && fs::exists(config().draftsDir / port.info.id / "portzone.json"))
        return std::nullopt;
    const std::string url = ownManifestUrl(port);
    const auto response   = http::get(url);
    Port updated          = port;
    if (response.error.empty() && response.status == 404)
    {
        // The repository has none: a store draft stays; a portzone.json the repository removed goes.
        if (!port.manifest || port.manifestSource != FROM_REPOSITORY)
            return std::nullopt;
        updated.manifest.reset();
        updated.manifestJson = nullptr;
        updated.manifestSource.clear();
        return updated;
    }
    if (!response.ok())
        return std::nullopt;
    json document;
    try
    {
        document = json::parse(response.body);
    }
    catch (const std::exception& e)
    {
        updated.manifestError = std::string("portzone.json: ") + e.what();
        return updated;
    }
    if (document == port.manifestJson)
        return std::nullopt;
    updated.manifestError.clear();
    setManifest(updated, document, url);
    return updated;
}

void Catalog::apply(Loaded loaded)
{
    ports  = std::move(loaded.ports);
    source = loaded.source;
}

void Catalog::replace(Port port)
{
    for (auto& existing : ports)
        if (existing.info.id == port.info.id)
            existing = std::move(port);
}

void Catalog::reapply()
{
    for (auto& port : ports)
        if (!port.manifestJson.is_null())
        {
            port.manifestError.clear();
            setManifest(port, json(port.manifestJson), std::string(port.manifestSource));
        }
}

} // namespace pz
