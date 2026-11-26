#include "images.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

#include "config.hpp"
#include "http.hpp"

namespace pz::images
{

namespace
{

    std::mutex memoryMutex;
    std::map<std::string, Bytes> memory;

    // Two downloads at a time: a homebrew started from the Album gets two network sessions.
    std::mutex gateMutex;
    std::condition_variable gateFree;
    int downloading = 0;

    struct Gate
    {
        Gate()
        {
            std::unique_lock<std::mutex> lock(gateMutex);
            gateFree.wait(lock, [] { return downloading < 2; });
            downloading++;
        }
        ~Gate()
        {
            std::lock_guard<std::mutex> lock(gateMutex);
            downloading--;
            gateFree.notify_one();
        }
    };

    bool isImage(const std::vector<unsigned char>& data)
    {
        static const unsigned char png[]  = { 0x89, 'P', 'N', 'G' };
        static const unsigned char jpeg[] = { 0xFF, 0xD8, 0xFF };
        return (data.size() > 4 && std::equal(png, png + 4, data.begin())) ||
            (data.size() > 3 && std::equal(jpeg, jpeg + 3, data.begin()));
    }

    fs::path cachePath(const std::string& url)
    {
        uint64_t hash = 1469598103934665603ull; // FNV-1a
        for (unsigned char c : url)
            hash = (hash ^ c) * 1099511628211ull;
        char name[32];
        std::snprintf(name, sizeof(name), "%016llx.img", static_cast<unsigned long long>(hash));
        return dataDir() / "cache" / "images" / name;
    }

    std::vector<unsigned char> readFile(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

} // namespace

Bytes get(const std::string& url, size_t maxBytes)
{
    if (url.empty())
        return nullptr;
    {
        std::lock_guard<std::mutex> lock(memoryMutex);
        const auto hit = memory.find(url);
        if (hit != memory.end())
            return hit->second;
    }

    const fs::path cached = cachePath(url);
    std::error_code ec;
    const bool haveCache = fs::exists(cached, ec);
    const bool fresh =
        haveCache && fs::file_time_type::clock::now() - fs::last_write_time(cached, ec) < std::chrono::hours(24 * 7);

    std::vector<unsigned char> data;
    if (fresh)
        data = readFile(cached);
    if (!isImage(data))
    {
        http::Response response;
        {
            Gate gate;
            response = http::get(url);
        }
        if (response.ok() && response.body.size() <= maxBytes)
        {
            data.assign(response.body.begin(), response.body.end());
            if (isImage(data))
            {
                fs::create_directories(cached.parent_path(), ec);
                std::ofstream(cached, std::ios::binary).write(response.body.data(), std::streamsize(response.body.size()));
            }
        }
        else if (haveCache)
            data = readFile(cached); // offline: a stale copy beats none
    }

    if (!isImage(data) || data.size() > maxBytes)
        return nullptr; // not remembered: the next view tries again
    Bytes result = std::make_shared<const std::vector<unsigned char>>(std::move(data));
    std::lock_guard<std::mutex> lock(memoryMutex);
    memory[url] = result;
    return result;
}

} // namespace pz::images
