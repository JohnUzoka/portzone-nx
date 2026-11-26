#include "gamefiles.hpp"

#include <algorithm>
#include <set>
#include <system_error>

#include <borealis/core/i18n.hpp>

#include "files.hpp"

namespace pz::gamefiles
{

namespace
{

    constexpr size_t MAX_DEPTH = 10;
    constexpr size_t MAX_FILES = 300000;

    // Top-level folders that hold the system, the CFW or their own data, never a player's game files.
    const std::set<std::string> SYSTEM_FOLDERS = {
        "atmosphere", "bootloader", "nintendo", "emummc", "emutendo", "sxos", "reinx", "$recycle.bin",
        "system volume information",
    };

    // `path` below the SD root, '/' separators, as typed: "Downloads/halo.iso".
    std::string sdRelative(const fs::path& path)
    {
        std::string root = config().sdRoot.generic_string();
        std::string full = path.generic_string();
        if (full.rfind(root, 0) == 0)
            full.erase(0, root.size());
        while (!full.empty() && full.front() == '/')
            full.erase(0, 1);
        return full;
    }

    // The folder all of `match`'s files come from (SD-relative), or "" when they share none.
    std::string commonFolder(const std::vector<matcher::Source>& sources, const matcher::Match& match)
    {
        std::string common;
        bool first = true;
        for (const auto& file : match.files)
        {
            const std::string& path = sources[file.source].path;
            const size_t slash      = path.rfind('/');
            const std::string dir   = slash == std::string::npos ? "" : path.substr(0, slash);
            if (first)
            {
                common = dir;
                first  = false;
                continue;
            }
            while (!common.empty() && lower(dir).rfind(lower(common) + "/", 0) != 0 && lower(dir) != lower(common))
            {
                const size_t slash = common.rfind('/');
                common             = slash == std::string::npos ? "" : common.substr(0, slash);
            }
        }
        return common;
    }

    // Removes `dir` and the folders above it while they are empty, up to and including `top` (both SD-relative).
    void removeEmpty(std::string dir, const std::string& top)
    {
        std::error_code ec;
        while (!dir.empty() && lower(dir).rfind(lower(top), 0) == 0 && fs::is_empty(sdPath(dir), ec) && !ec)
        {
            if (!fs::remove(sdPath(dir), ec))
                return;
            if (lower(dir) == lower(top))
                return;
            const size_t slash = dir.rfind('/');
            dir                = slash == std::string::npos ? "" : dir.substr(0, slash);
        }
    }

} // namespace

std::vector<matcher::Source> listSdCard(const std::vector<const Port*>& ports, const std::atomic<bool>& cancel,
    std::atomic<size_t>& seen)
{
    // Lowercase SD-relative folders whose files are already where they belong.
    std::set<std::string> skip = { "config/portzone" };
    for (const Port* port : ports)
        if (port->manifest && port->manifest->gameData)
            skip.insert(lower(sdRelative(sdPath(port->manifest->gameData->dir))));

    std::vector<matcher::Source> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(config().sdRoot, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && !cancel && out.size() < MAX_FILES; it.increment(ec))
    {
        const fs::directory_entry& entry = *it;
        const std::string relative       = sdRelative(entry.path());
        const std::string name           = entry.path().filename().string();
        std::error_code typeError;
        if (entry.is_directory(typeError))
        {
            const std::string key = lower(relative);
            if (name.empty() || name.front() == '.' || skip.count(key) ||
                (it.depth() == 0 && isSystemFolder(name)) || size_t(it.depth()) + 1 >= MAX_DEPTH)
                it.disable_recursion_pending();
            continue;
        }
        seen++;
        if (!entry.is_regular_file(typeError) || matcher::isJunk(name))
            continue;
        std::error_code sizeError;
        const uint64_t size = entry.file_size(sizeError);
        if (!sizeError)
            out.push_back({ relative, size });
    }
    return out;
}

bool isSystemFolder(const std::string& name) { return SYSTEM_FOLDERS.count(lower(name)) > 0; }

fs::path destination(const matcher::Match& match, const matcher::Placement& file)
{
    return sdPath(match.gameDir) / fs::path(file.dest);
}

std::shared_ptr<Job> startMove(std::vector<matcher::Source> sources, std::vector<matcher::Match> chosen)
{
    return startJob("@gamefiles", [sources = std::move(sources), chosen = std::move(chosen)](Job& job) {
        size_t total = 0;
        for (const auto& match : chosen)
            total += match.files.size();

        size_t done = 0, moved = 0;
        std::vector<std::string> problems;
        // A folder that came over whole is removed once empty; a single file's folder (Downloads/) stays.
        std::vector<std::pair<std::string, std::string>> emptied; // SD-relative folder, the top to stop at
        for (const auto& match : chosen)
        {
            job.setStage(brls::getStr("portzone/jobs/moving", match.inputLabel));
            const std::string top = match.files.size() > 1 ? commonFolder(sources, match) : "";
            for (const auto& file : match.files)
            {
                if (job.cancel)
                    break;
                const matcher::Source& source = sources[file.source];
                const fs::path from           = sdPath(source.path);
                const fs::path to             = destination(match, file);
                job.fraction                  = float(done++) / float(std::max<size_t>(total, 1));

                std::error_code ec;
                if (!fs::is_regular_file(from, ec))
                {
                    problems.push_back(brls::getStr("portzone/jobs/gone", source.path));
                    continue;
                }
                if (!file.sha256.empty() && fs::file_size(from, ec) <= HASH_LIMIT)
                {
                    const std::string hash = sha256Of(from);
                    if (std::find(file.sha256.begin(), file.sha256.end(), hash) == file.sha256.end())
                    {
                        problems.push_back(brls::getStr("portzone/jobs/unknown_version", source.path));
                        continue;
                    }
                }
                if (const std::string error = moveFile(from, to); !error.empty())
                {
                    problems.push_back(brls::getStr("portzone/jobs/could_not_move", source.path, error));
                    continue;
                }
                if (const size_t slash = source.path.rfind('/'); !top.empty() && slash != std::string::npos)
                    emptied.emplace_back(source.path.substr(0, slash), top);
                moved++;
            }
        }
        std::sort(emptied.begin(), emptied.end());
        emptied.erase(std::unique(emptied.begin(), emptied.end()), emptied.end());
        for (auto it = emptied.rbegin(); it != emptied.rend(); ++it) // deepest first
            removeEmpty(it->first, it->second);

        job.fraction = 1;
        job.setStage(brls::getStr(moved == 1 ? "portzone/jobs/moved_one" : "portzone/jobs/moved_other", moved));
        if (job.cancel)
            job.fail(brls::getStr(moved == 1 ? "portzone/jobs/stopped_one" : "portzone/jobs/stopped_other", moved));
        else if (!problems.empty())
            job.fail(problems.size() > 1 ? brls::getStr("portzone/jobs/and_more", problems.front(), problems.size() - 1) :
                                           problems.front());
    });
}

} // namespace pz::gamefiles
