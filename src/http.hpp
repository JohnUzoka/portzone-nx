#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace pz::http
{

struct Response
{
    long status = 0;
    std::string body;
    std::string error;                         // set when the request did not complete
    std::map<std::string, std::string> headers; // names lowercased

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

void init();
void cleanup();

Response get(const std::string& url, const std::vector<std::string>& headers = {});

// Return false to cancel.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

// Streams to `to`; the body of the response is left empty.
Response download(const std::string& url, const std::filesystem::path& to, const Progress& progress);

} // namespace pz::http
