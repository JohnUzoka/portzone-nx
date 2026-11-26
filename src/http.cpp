#include "http.hpp"

#include <curl/curl.h>

#include <borealis/core/logger.hpp>

#include <cstdio>

#ifndef PORTZONE_VERSION
#define PORTZONE_VERSION "dev"
#endif

namespace pz::http
{

namespace
{

    size_t onHeader(char* data, size_t size, size_t count, void* user)
    {
        auto* headers = static_cast<std::map<std::string, std::string>*>(user);
        std::string line(data, size * count);
        const auto colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string name = line.substr(0, colon);
            for (char& c : name)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            std::string value = line.substr(colon + 1);
            const auto first  = value.find_first_not_of(" \t");
            const auto last   = value.find_last_not_of(" \t\r\n");
            (*headers)[name]  = first == std::string::npos ? "" : value.substr(first, last - first + 1);
        }
        return size * count;
    }

    size_t onBody(char* data, size_t size, size_t count, void* user)
    {
        static_cast<std::string*>(user)->append(data, size * count);
        return size * count;
    }

    size_t onFile(char* data, size_t size, size_t count, void* user)
    {
        return std::fwrite(data, size, count, static_cast<FILE*>(user)) * size;
    }

    int onProgress(void* user, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t)
    {
        const auto* progress = static_cast<const Progress*>(user);
        return (*progress)(uint64_t(done), uint64_t(total)) ? 0 : 1;
    }

    CURL* prepare(const std::string& url, curl_slist* headerList, Response& response)
    {
        CURL* curl = curl_easy_init();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "portzone/" PORTZONE_VERSION);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, onHeader);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
#ifdef __SWITCH__
        // Mozilla's CA bundle from curl.se, shipped in the app's romfs.
        curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/certs/cacert.pem");
#endif
        return curl;
    }

    void finish(const std::string& url, CURL* curl, CURLcode code, Response& response)
    {
        if (code != CURLE_OK)
            response.error = curl_easy_strerror(code);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
        curl_easy_cleanup(curl);
        if (!response.ok())
            brls::Logger::debug("http: {} -> {} {}", url, response.status, response.error);
    }

    curl_slist* toList(const std::vector<std::string>& headers)
    {
        curl_slist* list = nullptr;
        for (const auto& header : headers)
            list = curl_slist_append(list, header.c_str());
        return list;
    }

} // namespace

void init() { curl_global_init(CURL_GLOBAL_DEFAULT); }

void cleanup() { curl_global_cleanup(); }

Response get(const std::string& url, const std::vector<std::string>& headers)
{
    Response response;
    curl_slist* list = toList(headers);
    CURL* curl       = prepare(url, list, response);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    finish(url, curl, curl_easy_perform(curl), response);
    curl_slist_free_all(list);
    return response;
}

Response download(const std::string& url, const std::filesystem::path& to, const Progress& progress)
{
    Response response;
    FILE* file = std::fopen(to.string().c_str(), "wb");
    if (!file)
    {
        response.error = "cannot write " + to.string();
        return response;
    }
    std::setvbuf(file, nullptr, _IOFBF, 1 << 20);
    CURL* curl = prepare(url, nullptr, response);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    const CURLcode code = curl_easy_perform(curl);
    if (std::fclose(file) != 0 && code == CURLE_OK)
        response.error = "cannot finish writing " + to.string();
    finish(url, curl, code, response);
    if (code == CURLE_ABORTED_BY_CALLBACK)
        response.error = "cancelled";
    return response;
}

} // namespace pz::http
