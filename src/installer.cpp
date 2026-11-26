#include "installer.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <json.hpp>
#include <miniz.h>
#include <picosha2.h>

#include "config.hpp"
#include "files.hpp"
#include "http.hpp"
#include "threads.hpp"

namespace pz
{

using nlohmann::json;

namespace
{

    std::mutex jobsMutex;
    std::map<std::string, std::shared_ptr<Job>> jobs;

    fs::path recordPath(const std::string& id) { return dataDir() / "installed" / (id + ".json"); }

    bool startsWithNoCase(const std::string& text, const std::string& prefix)
    {
        return text.size() >= prefix.size() && lower(text.substr(0, prefix.size())) == lower(prefix);
    }

    // A zip entry or record path that stays inside its folder.
    bool safeRelative(const std::string& path)
    {
        if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos)
            return false;
        std::istringstream parts(path);
        std::string part;
        while (std::getline(parts, part, '/'))
            if (part == "..")
                return false;
        return true;
    }

    std::string sha256Of(const fs::path& path, Job& job)
    {
        std::ifstream file(path, std::ios::binary);
        const uint64_t total = fs::file_size(path);
        uint64_t done        = 0;
        picosha2::hash256_one_by_one hasher;
        std::vector<char> buffer(1 << 20);
        while (file)
        {
            file.read(buffer.data(), std::streamsize(buffer.size()));
            hasher.process(buffer.begin(), buffer.begin() + file.gcount());
            done += uint64_t(file.gcount());
            job.fraction = 0.8f + 0.05f * float(done) / float(std::max<uint64_t>(total, 1));
        }
        hasher.finish();
        return picosha2::get_hash_hex_string(hasher);
    }

    void saveRecord(const InstallRecord& record)
    {
        json out = { { "id", record.id }, { "tag", record.tag }, { "asset", record.asset }, { "target", record.target },
            { "files", record.files } };
        std::error_code ec;
        fs::create_directories(recordPath(record.id).parent_path(), ec);
        std::ofstream(recordPath(record.id)) << out.dump(2) << "\n";
    }

    // Removes `relative` (under the SD root), then its parent folders while they are empty.
    // Never the SD root or the top switch/ folder.
    void removeWithEmptyParents(const std::string& relative)
    {
        std::error_code ec;
        fs::remove(config().sdRoot / relative, ec);
        fs::path parent = fs::path(relative).parent_path();
        while (!parent.empty() && lower(parent.string()) != "switch")
        {
            const fs::path real = config().sdRoot / parent;
            if (!fs::is_directory(real, ec) || !fs::is_empty(real, ec) || !fs::remove(real, ec))
                break;
            parent = parent.parent_path();
        }
    }

    // Extracts `step`'s zip. Normally everything under `from` goes into `dest`, keeping the zip's folders. A manual
    // install (guessLayout) does what Sphaira does instead: a switch/ folder goes to the SD root (loose files are
    // left out); otherwise everything goes to `dest`, without a single shared top folder.
    std::string extract(const fs::path& zipPath, const PlanStep& step, Job& job, float start, float end,
        std::vector<std::string>& files)
    {
        mz_zip_archive zip {};
        if (!mz_zip_reader_init_file(&zip, zipPath.string().c_str(), 0))
            return step.asset.name + " is not a readable zip.";

        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        std::vector<std::string> names;
        uint64_t totalSize = 0;
        for (mz_uint i = 0; i < count; i++)
        {
            mz_zip_archive_file_stat stat;
            mz_zip_reader_file_stat(&zip, i, &stat);
            names.emplace_back(stat.m_filename);
            totalSize += stat.m_uncomp_size;
        }

        std::string strip = step.from;
        bool sdRoot       = false;
        if (step.guessLayout)
        {
            std::string root;
            bool shared = !names.empty();
            for (const auto& name : names)
            {
                const auto slash = name.find('/');
                if (slash == std::string::npos || slash == 0 || (!root.empty() && name.substr(0, slash) != root))
                {
                    shared = false;
                    break;
                }
                root = name.substr(0, slash);
            }
            if (shared && lower(root) != "switch")
                strip = root + "/";
            for (const auto& name : names)
                sdRoot |= startsWithNoCase(name, strip) && startsWithNoCase(name.substr(strip.size()), "switch/");
        }

        const std::string destShown = sdRoot ? "" : sdShown(step.dest);
        uint64_t done               = 0;
        size_t extracted            = 0;
        std::string error;
        for (mz_uint i = 0; i < count && error.empty(); i++)
        {
            mz_zip_archive_file_stat stat;
            mz_zip_reader_file_stat(&zip, i, &stat);
            const std::string& name = names[i];
            done += stat.m_uncomp_size;
            if (stat.m_is_directory || !startsWithNoCase(name, strip))
                continue;
            const std::string rest = name.substr(strip.size());
            if (rest.empty() || !safeRelative(rest) || (sdRoot && !startsWithNoCase(rest, "switch/")))
                continue;

            const std::string relative = destShown.empty() ? rest : destShown + "/" + rest;
            const fs::path out         = config().sdRoot / relative;
            std::error_code ec;
            fs::create_directories(out.parent_path(), ec);
            job.setStage("Extracting " + rest);
            if (!mz_zip_reader_extract_to_file(&zip, i, out.string().c_str(), 0))
                error = "Could not extract " + rest + ".";
            else
            {
                files.push_back(relative);
                extracted++;
            }
            job.fraction = start + (end - start) * float(done) / float(std::max<uint64_t>(totalSize, 1));
        }
        mz_zip_reader_end(&zip);
        if (error.empty() && extracted == 0)
            error = step.from.empty() ? step.asset.name + " has nothing to install." :
                                        step.asset.name + " has no folder " + step.from + ".";
        return error;
    }

    // Puts the downloaded file in place (a rename: it is on the same SD card).
    std::string copyFile(const fs::path& download, const PlanStep& step, std::vector<std::string>& files)
    {
        const std::string folder   = sdShown(step.dest);
        const std::string fileName = step.name.empty() ? step.asset.name : step.name;
        const std::string relative = folder.empty() ? fileName : folder + "/" + fileName;
        if (const std::string error = moveFile(download, config().sdRoot / relative); !error.empty())
            return "Could not write " + relative + ": " + error;
        files.push_back(relative);
        return "";
    }

    void runInstall(const InstallPlan& plan, Job& job)
    {
        std::error_code ec;
        const fs::path tmp = dataDir() / "tmp";
        fs::create_directories(tmp, ec);
        std::vector<fs::path> downloads;
        auto cleanUp = [&downloads] {
            std::error_code ignored;
            for (const auto& path : downloads)
                fs::remove(path, ignored);
        };

        // Every file first, so a failed or cancelled download leaves the SD card as it was.
        uint64_t total = 0, before = 0;
        for (const auto& step : plan.steps)
            total += step.asset.size;
        for (size_t i = 0; i < plan.steps.size(); i++)
        {
            const auto& asset = plan.steps[i].asset;
            downloads.push_back(tmp / (plan.id + "." + std::to_string(i) + ".download"));
            const auto response = http::download(asset.url, downloads.back(), [&](uint64_t done, uint64_t) {
                job.fraction = 0.8f * float(before + done) / float(std::max<uint64_t>(total, 1));
                job.setStage("Downloading " + asset.name + ": " + formatSize(done) + " of " + formatSize(asset.size));
                return !job.cancel.load();
            });
            if (!response.ok())
            {
                cleanUp();
                job.fail(response.error == "cancelled" ? "Cancelled." :
                    "Downloading " + asset.name + " failed: " +
                        (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error));
                return;
            }
            before += asset.size;
            if (!asset.sha256.empty())
            {
                job.setStage("Checking " + asset.name);
                if (sha256Of(downloads.back(), job) != lower(asset.sha256))
                {
                    cleanUp();
                    job.fail(asset.name + " does not match GitHub's checksum. Nothing was installed.");
                    return;
                }
            }
        }

        std::vector<std::string> files;
        for (size_t i = 0; i < plan.steps.size(); i++)
        {
            const PlanStep& step = plan.steps[i];
            const float start    = 0.85f + 0.15f * float(i) / float(plan.steps.size());
            const float end      = 0.85f + 0.15f * float(i + 1) / float(plan.steps.size());
            job.setStage((step.unzip ? "Extracting " : "Copying ") + step.asset.name);
            const std::string error = step.unzip ? extract(downloads[i], step, job, start, end, files) :
                                                   copyFile(downloads[i], step, files);
            if (!error.empty())
            {
                cleanUp();
                // What earlier steps wrote stays on the card: record it, so Remove can take it away.
                if (!files.empty())
                    saveRecord({ plan.id, plan.tag + " (incomplete)", plan.steps.front().asset.name, plan.steps.front().dest, files });
                job.fail(error);
                return;
            }
            job.fraction = end;
        }
        cleanUp();

        // One entry per file, though a later step may have overwritten an earlier one's.
        std::set<std::string> seen;
        std::vector<std::string> unique;
        for (const auto& file : files)
            if (seen.insert(lower(file)).second)
                unique.push_back(file);

        // Files the previous version had and this one does not.
        if (const auto previous = loadRecord(plan.id))
            for (const auto& old : previous->files)
                if (!seen.count(lower(old)) && safeRelative(old))
                    removeWithEmptyParents(old);

        saveRecord({ plan.id, plan.tag, plan.steps.front().asset.name, plan.steps.front().dest, unique });
        job.fraction = 1;
        job.setStage("Installed " + plan.tag);
    }

    void runRemove(const std::string& id, Job& job)
    {
        const auto record = loadRecord(id);
        if (!record)
        {
            job.fail("PortZone has no record of installing this port.");
            return;
        }
        job.setStage("Removing");
        for (size_t i = 0; i < record->files.size(); i++)
        {
            if (safeRelative(record->files[i]))
                removeWithEmptyParents(record->files[i]);
            job.fraction = float(i + 1) / float(record->files.size());
        }
        std::error_code ec;
        fs::remove(recordPath(id), ec);
        job.setStage("Removed");
    }

    void runSelfUpdate(const github::Asset& asset, const std::string& tag, Job& job)
    {
        const fs::path self = config().selfPath;
        if (self.empty())
        {
            job.fail("This copy of PortZone does not know where its .nro is, so it cannot replace it.");
            return;
        }
        // Next to the old .nro, so the swap is a rename on the same card.
        const fs::path fresh = self.string() + ".download";
        const fs::path old   = self.string() + ".old";
        std::error_code ec;

        const auto response = http::download(asset.url, fresh, [&](uint64_t done, uint64_t total) {
            job.fraction = 0.9f * float(done) / float(std::max<uint64_t>(total, 1));
            job.setStage("Downloading PortZone " + tag + ": " + formatSize(done) + " of " + formatSize(total));
            return !job.cancel.load();
        });
        if (!response.ok())
        {
            fs::remove(fresh, ec);
            job.fail(response.error == "cancelled" ? "Cancelled." :
                "The download failed: " + (response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error));
            return;
        }
        if (!asset.sha256.empty() && sha256Of(fresh, job) != lower(asset.sha256))
        {
            fs::remove(fresh, ec);
            job.fail("The download does not match GitHub's checksum. PortZone was not changed.");
            return;
        }

        // FAT cannot rename over a file: move the old one aside, put the new one in, then drop the old one.
        fs::remove(old, ec);
        fs::rename(self, old, ec);
        if (ec)
        {
            fs::remove(fresh, ec);
            job.fail("Could not move the old PortZone aside: " + ec.message());
            return;
        }
        fs::rename(fresh, self, ec);
        if (ec)
        {
            std::error_code ignored;
            fs::rename(old, self, ignored);
            fs::remove(fresh, ignored);
            job.fail("Could not put the new PortZone in place: " + ec.message());
            return;
        }
        fs::remove(old, ec);
        job.fraction = 1;
        job.setStage("Updated to " + tag);
    }

} // namespace

std::shared_ptr<Job> startJob(const std::string& id, std::function<void(Job&)> work)
{
    std::lock_guard<std::mutex> lock(jobsMutex);
    const auto running = jobs.find(id);
    if (running != jobs.end() && !running->second->finished)
        return nullptr;
    auto job = std::make_shared<Job>();
    jobs[id] = job;
    runInBackground([job, work = std::move(work)] {
        work(*job);
        job->finished = true;
    });
    return job;
}

std::string Job::stage() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return stageText;
}

std::string Job::error() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return errorText;
}

void Job::setStage(std::string text)
{
    std::lock_guard<std::mutex> lock(mutex);
    stageText = std::move(text);
}

void Job::fail(std::string text)
{
    std::lock_guard<std::mutex> lock(mutex);
    errorText = std::move(text);
}

std::optional<InstallRecord> loadRecord(const std::string& id)
{
    std::ifstream file(recordPath(id));
    if (!file)
        return std::nullopt;
    try
    {
        const json in = json::parse(file);
        InstallRecord record;
        record.id     = in.value("id", id);
        record.tag    = in.value("tag", "");
        record.asset  = in.value("asset", "");
        record.target = in.value("target", "");
        record.files  = in.value("files", std::vector<std::string>());
        return record;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::shared_ptr<Job> startInstall(InstallPlan plan)
{
    const std::string id = plan.id;
    return startJob(id, [plan = std::move(plan)](Job& job) { runInstall(plan, job); });
}

std::shared_ptr<Job> startRemove(const std::string& id)
{
    return startJob(id, [id](Job& job) { runRemove(id, job); });
}

std::shared_ptr<Job> startSelfUpdate(github::Asset asset, std::string tag)
{
    // "@" cannot start a port id, so this never meets a port's job.
    return startJob("@portzone", [asset = std::move(asset), tag = std::move(tag)](Job& job) { runSelfUpdate(asset, tag, job); });
}

std::shared_ptr<Job> jobFor(const std::string& id)
{
    std::lock_guard<std::mutex> lock(jobsMutex);
    const auto found = jobs.find(id);
    return found == jobs.end() ? nullptr : found->second;
}

} // namespace pz
