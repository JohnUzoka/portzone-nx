#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "github.hpp"

namespace pz
{

// What PortZone installed for a port: <data>/installed/<id>.json. Game files are never in it.
struct InstallRecord
{
    std::string id, tag, asset, target;
    std::vector<std::string> files; // relative to the SD root, '/' separators
};

std::optional<InstallRecord> loadRecord(const std::string& id);

// One release file and what to do with it.
struct PlanStep
{
    github::Asset asset;
    bool unzip = false;       // otherwise copy
    std::string dest;         // sdmc:/ folder
    std::string from;         // unzip: folder of the zip to take, ending in /
    std::string name;         // copy: file name in dest; empty keeps the asset's
    bool guessLayout = false; // unzip a manual install the way Sphaira does (see extract)
};

// Steps run in order after every file has downloaded and passed its checksum.
struct InstallPlan
{
    std::string id, tag;
    std::vector<PlanStep> steps;
};

// A running install or removal. Read from the UI thread; written by the worker.
class Job
{
  public:
    std::atomic<bool> cancel { false };   // honoured while downloading
    std::atomic<bool> finished { false };
    std::atomic<float> fraction { 0 };

    std::string stage() const;
    std::string error() const;
    void setStage(std::string text);
    void fail(std::string text);

  private:
    mutable std::mutex mutex;
    std::string stageText, errorText;
};

// One job per port at a time; nullptr when one is already running.
std::shared_ptr<Job> startInstall(InstallPlan plan);
std::shared_ptr<Job> startRemove(const std::string& id);

// PortZone's own update: downloads `asset`, checks it, and puts it in place of config().selfPath.
std::shared_ptr<Job> startSelfUpdate(github::Asset asset, std::string tag);

// The job running (or just finished) for a port, if any.
std::shared_ptr<Job> jobFor(const std::string& id);

// Runs `work` as the job for `id` ("@name" for jobs that are not a port's). nullptr when that job is still running.
std::shared_ptr<Job> startJob(const std::string& id, std::function<void(Job&)> work);

} // namespace pz
