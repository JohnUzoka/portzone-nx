#include "model.hpp"

#include <stdexcept>

namespace pz
{

using nlohmann::json;

namespace
{

    std::string str(const json& object, const char* key)
    {
        const auto it = object.find(key);
        return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    std::string required(const json& object, const char* key)
    {
        std::string value = str(object, key);
        if (value.empty())
            throw std::runtime_error(std::string("missing \"") + key + "\"");
        return value;
    }

    std::vector<std::string> strings(const json& object, const char* key)
    {
        std::vector<std::string> out;
        const auto it = object.find(key);
        if (it != object.end() && it->is_array())
            for (const auto& item : *it)
                if (item.is_string())
                    out.push_back(item.get<std::string>());
        return out;
    }

    // Paths in portstore.json are relative and never climb out of their folder.
    std::string relative(std::string path)
    {
        if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
            path.find(':') != std::string::npos)
            throw std::runtime_error("unsafe path \"" + path + "\"");
        size_t start = 0;
        while (start <= path.size())
        {
            const size_t end = std::min(path.find('/', start), path.size());
            if (path.compare(start, end - start, "..") == 0 && end - start == 2)
                throw std::runtime_error("unsafe path \"" + path + "\"");
            start = end + 1;
        }
        return path;
    }

    std::string sdmc(const json& object, const char* key)
    {
        std::string value = required(object, key);
        if (value.rfind("sdmc:/", 0) != 0)
            throw std::runtime_error(std::string("\"") + key + "\" must start with sdmc:/");
        if (value.size() > 6)
            relative(value.substr(6));
        return value;
    }

    Platform platform(const json& object)
    {
        Platform result;
        const auto it = object.find("platform");
        if (it == object.end() || !it->is_object())
            return result;
        result.present  = true;
        result.original = str(*it, "original");
        result.runsOn   = strings(*it, "runs_on");
        return result;
    }

} // namespace

Manifest parseManifest(const json& root)
{
    if (!root.is_object() || root.value("format", 0) != 1)
        throw std::runtime_error("not a format 1 portzone.json");

    Manifest store;
    store.description = str(root, "description");
    store.icon        = str(root, "icon");
    store.banner      = str(root, "banner");
    store.platform    = platform(root);
    for (const std::string* image : { &store.icon, &store.banner })
        if (!image->empty())
            relative(*image);

    if (root.contains("release"))
        store.prerelease = root.at("release").value("prerelease", false);

    const auto& install = root.at("install");
    store.binary        = sdmc(install, "binary");
    if (install.contains("steps"))
        for (const auto& item : install.at("steps"))
        {
            InstallStep step;
            step.asset = required(item, "asset");
            if (item.contains("unzip"))
            {
                step.kind = InstallStep::Kind::Unzip;
                step.dest = sdmc(item, "unzip");
                step.from = str(item, "from");
                if (!step.from.empty())
                    relative(step.from);
            }
            else
            {
                step.dest = sdmc(item, "copy");
                step.name = str(item, "name");
                if (step.name.find_first_of("/\\:") != std::string::npos)
                    throw std::runtime_error("unsafe file name \"" + step.name + "\"");
            }
            store.installSteps.push_back(std::move(step));
        }

    if (root.contains("launch"))
    {
        const auto& launch = root.at("launch");
        store.fullMemory   = launch.value("full_memory", true);
        store.launchNotes  = strings(launch, "notes");
    }

    if (root.contains("game_data"))
    {
        const auto& data = root.at("game_data");
        GameData game;
        game.dir = sdmc(data, "dir");
        for (const auto& item : data.at("inputs"))
        {
            Input input { required(item, "id"), required(item, "label"), item.value("required", true), {} };
            for (const auto& opt : item.at("options"))
            {
                Option option { required(opt, "label"), str(opt, "url"), {} };
                for (const auto& file : opt.at("files"))
                {
                    FileSpec spec;
                    spec.path  = relative(required(file, "path"));
                    spec.match = str(file, "match");
                    for (auto& marker : strings(file, "markers"))
                        spec.markers.push_back(relative(marker));
                    if ((!spec.match.empty() || !spec.markers.empty()) && !spec.isFolder())
                        throw std::runtime_error("\"match\" and \"markers\" need a folder path (ending in /): " + spec.path);
                    spec.note  = str(file, "note");
                    if (file.contains("size"))
                        spec.size = file.at("size").get<uint64_t>();
                    spec.sha256 = strings(file, "sha256");
                    option.files.push_back(std::move(spec));
                }
                input.options.push_back(std::move(option));
            }
            if (input.options.empty())
                throw std::runtime_error("input \"" + input.id + "\" has no options");
            game.inputs.push_back(std::move(input));
        }
        if (data.contains("prepare"))
        {
            const auto& p = data.at("prepare");
            Prepare prepare;
            prepare.where   = required(p, "where");
            prepare.summary = required(p, "summary");
            if (p.contains("tool"))
            {
                prepare.toolName = required(p.at("tool"), "name");
                prepare.toolUrl  = required(p.at("tool"), "url");
            }
            for (auto& done : strings(p, "done"))
                prepare.done.push_back(relative(done));
            game.prepare = std::move(prepare);
        }
        if (data.contains("generated"))
            for (const auto& item : data.at("generated"))
                game.generated.push_back({ relative(required(item, "path")), str(item, "note") });
        store.gameData = std::move(game);
    }

    store.steps = strings(root, "steps");
    if (store.steps.empty())
        throw std::runtime_error("missing \"steps\"");
    return store;
}

std::vector<IndexEntry> parseIndex(const json& root)
{
    std::vector<IndexEntry> entries;
    for (const auto& item : root.at("ports"))
    {
        IndexEntry entry;
        entry.id          = required(item, "id");
        entry.url         = required(item, "url");
        entry.title       = required(item, "title");
        entry.author      = str(item, "author");
        entry.description = str(item, "description");
        entry.icon        = str(item, "icon");
        entry.banner      = str(item, "banner");
        if (const std::string manifest = str(item, "manifest"); !manifest.empty())
            entry.manifest = relative(manifest);
        entry.platform    = platform(item);
        if (entry.url.rfind("https://github.com/", 0) != 0)
            throw std::runtime_error(entry.id + ": url must be a GitHub repository");
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace pz
