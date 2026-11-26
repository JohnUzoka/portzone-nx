#include "transfer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <thread>

#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/core/logger.hpp>
#include <json.hpp>
#include <picosha2.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "config.hpp"
#include "files.hpp"
#include "gamefiles.hpp"
#include "installer.hpp"
#include "matcher.hpp"
#include "state.hpp"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace pz::transfer
{

using nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace
{

    constexpr int FIRST_PORT          = 8080;
    constexpr int LAST_PORT           = 8089;
    constexpr size_t HEAD_LIMIT       = 64 << 10;
    constexpr uint64_t PLAN_LIMIT     = 64ull << 20;
    constexpr size_t RECV_BUFFER      = 256 << 10;
    constexpr size_t WRITE_BUFFER     = 1 << 20;
    constexpr size_t LOG_LINES        = 50;
    constexpr auto IDLE_LIMIT         = std::chrono::seconds(30);
    constexpr int POLL_MS             = 200;
    constexpr const char* NO_NETWORK  = "Connect the Switch to a Wi-Fi network first (the same one as your phone or PC).";
    constexpr const char* KEY_LETTERS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // 32: no I, O, 0, 1

    struct Server
    {
        std::mutex mutex; // guards status
        Status status;

        std::thread thread;
        std::atomic<bool> stopping{false};
        int listenFd = -1;

        // Owned by the server thread while it runs.
        std::vector<Port> ports;
        std::string focus, key;
        unsigned planNumber = 0;
        bool planned        = false;
        std::vector<matcher::Source> sources;
        std::vector<matcher::Match> matches;
        std::set<size_t> counted;                       // matches whose files make up filesTotal/bytesTotal
        std::set<std::pair<size_t, size_t>> received;   // (match, file)
        size_t failed = 0;

        ~Server()
        {
            stopping = true;
            if (thread.joinable())
                thread.join();
            if (listenFd >= 0)
                close(listenFd);
        }
    };

    Server& server()
    {
        static Server instance;
        return instance;
    }

    fs::path partPath() { return dataDir() / "tmp" / "upload.part"; }

    template <typename Change>
    void update(Change change)
    {
        Server& s = server();
        std::lock_guard<std::mutex> lock(s.mutex);
        change(s.status);
        ++s.status.generation;
    }

    void addLog(Status& status, std::string line)
    {
        brls::Logger::info("transfer: {}", line);
        status.log.push_back(std::move(line));
        if (status.log.size() > LOG_LINES)
            status.log.erase(status.log.begin(), status.log.end() - LOG_LINES);
    }

    std::string makeKey()
    {
        unsigned char bytes[6];
#ifdef __SWITCH__
        randomGet(bytes, sizeof(bytes));
#else
        std::random_device random;
        for (unsigned char& b : bytes)
            b = static_cast<unsigned char>(random());
#endif
        std::string key;
        for (unsigned char b : bytes)
            key += KEY_LETTERS[b & 31];
        return key;
    }

    std::string fileName(const std::string& path)
    {
        const auto slash = path.rfind('/');
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    struct Request
    {
        std::string method, path;
        std::map<std::string, std::string> query;
        std::map<std::string, std::string> headers; // lowercase names
        bool hasLength  = false;
        uint64_t length = 0;
    };

    struct Connection
    {
        int fd = -1;
        std::string pending;   // bytes received after the head
        uint64_t bodyLeft = 0; // body bytes not read yet
    };

    // Waits until `fd` is ready for `events`; false on stop, error or 30 s of silence.
    bool waitFor(int fd, short events)
    {
        const auto deadline = Clock::now() + IDLE_LIMIT;
        while (!server().stopping)
        {
            pollfd p{fd, events, 0};
            const int ready = poll(&p, 1, POLL_MS);
            if (ready < 0)
                return false;
            if (ready > 0)
                return (p.revents & (events | POLLHUP)) != 0;
            if (Clock::now() >= deadline)
                return false;
        }
        return false;
    }

    // EINTR, or EAGAIN after a poll that said ready, is not an error: try again.
    bool retryable(ssize_t result) { return result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR); }

    // Bytes received, 0 when the client is gone, stopped or silent.
    size_t receive(int fd, char* out, size_t max)
    {
        for (;;)
        {
            if (!waitFor(fd, POLLIN))
                return 0;
            const auto got = recv(fd, out, max, 0);
            if (!retryable(got))
                return got > 0 ? size_t(got) : 0;
        }
    }

    bool sendAll(int fd, const std::string& data)
    {
        size_t sent = 0;
        while (sent < data.size())
        {
            if (!waitFor(fd, POLLOUT))
                return false;
            const auto n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
            if (retryable(n))
                continue;
            if (n <= 0)
                return false;
            sent += size_t(n);
        }
        return true;
    }

    // Up to `max` body bytes; 0 at the end of the body or when the client is gone.
    size_t readBody(Connection& c, char* out, size_t max)
    {
        max = size_t(std::min<uint64_t>(max, c.bodyLeft));
        if (max == 0)
            return 0;
        size_t got = 0;
        if (!c.pending.empty())
        {
            got = std::min(max, c.pending.size());
            std::copy(c.pending.begin(), c.pending.begin() + std::ptrdiff_t(got), out);
            c.pending.erase(0, got);
        }
        else
            got = receive(c.fd, out, max);
        c.bodyLeft -= got;
        return got;
    }

    bool readWholeBody(Connection& c, std::string& out)
    {
        out.resize(size_t(c.bodyLeft));
        size_t done = 0;
        while (c.bodyLeft > 0)
        {
            const size_t got = readBody(c, &out[done], out.size() - done);
            if (got == 0)
                return false;
            done += got;
        }
        return true;
    }

    const char* reason(int code)
    {
        switch (code)
        {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 409: return "Conflict";
            case 410: return "Gone";
            case 411: return "Length Required";
            case 413: return "Payload Too Large";
            case 431: return "Request Header Fields Too Large";
            default: return "Internal Server Error";
        }
    }

    void respond(Connection& c, int code, const std::string& type, const std::string& body)
    {
        std::ostringstream head;
        head << "HTTP/1.1 " << code << ' ' << reason(code) << "\r\n"
             << "Content-Type: " << type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Cache-Control: no-store\r\n"
             << "Connection: close\r\n\r\n";
        sendAll(c.fd, head.str() + body);
    }

    void respondJson(Connection& c, int code, const json& body) { respond(c, code, "application/json", body.dump()); }

    void respondError(Connection& c, int code, const std::string& message) { respondJson(c, code, {{"error", message}}); }

    // A client still sending a body when the reply is already out would get a reset instead of the reply: read
    // (and drop) what it keeps sending for a while before closing.
    void drain(Connection& c)
    {
        if (c.bodyLeft == 0)
            return;
        shutdown(c.fd, SHUT_WR);
        std::vector<char> sink(64 << 10);
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (c.bodyLeft > 0 && Clock::now() < deadline)
            if (readBody(c, sink.data(), sink.size()) == 0)
                break;
    }

    int hexValue(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    std::string percentDecode(const std::string& text)
    {
        std::string out;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '+')
                out += ' ';
            else if (text[i] == '%' && i + 2 < text.size() && hexValue(text[i + 1]) >= 0 && hexValue(text[i + 2]) >= 0)
            {
                out += char(hexValue(text[i + 1]) * 16 + hexValue(text[i + 2]));
                i += 2;
            }
            else
                out += text[i];
        }
        return out;
    }

    std::string trim(const std::string& text)
    {
        const auto first = text.find_first_not_of(" \t");
        const auto last  = text.find_last_not_of(" \t\r");
        return first == std::string::npos ? "" : text.substr(first, last - first + 1);
    }

    bool parseNumber(const std::string& text, uint64_t& out)
    {
        if (text.empty() || text.size() > 19 || text.find_first_not_of("0123456789") != std::string::npos)
            return false;
        out = std::stoull(text);
        return true;
    }

    // Reads the request line and headers. Returns 0, or the error status to reply with (-1: nobody to reply to).
    int readHead(Connection& c, Request& req)
    {
        std::string head;
        std::vector<char> buffer(16 << 10);
        size_t end;
        while ((end = head.find("\r\n\r\n")) == std::string::npos)
        {
            if (head.size() > HEAD_LIMIT)
                return 431;
            const size_t got = receive(c.fd, buffer.data(), buffer.size());
            if (got == 0)
                return -1;
            head.append(buffer.data(), got);
        }
        if (end + 4 > HEAD_LIMIT)
            return 431;
        c.pending = head.substr(end + 4);
        head.resize(end);

        std::istringstream lines(head);
        std::string line, target, version;
        std::getline(lines, line);
        std::istringstream first(line);
        if (!(first >> req.method >> target >> version))
            return 400;
        while (std::getline(lines, line))
        {
            const auto colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        }

        const auto question = target.find('?');
        req.path            = percentDecode(target.substr(0, question));
        if (question != std::string::npos)
        {
            std::istringstream params(target.substr(question + 1));
            std::string param;
            while (std::getline(params, param, '&'))
            {
                const auto equals = param.find('=');
                if (equals == std::string::npos)
                    req.query[percentDecode(param)] = "";
                else
                    req.query[percentDecode(param.substr(0, equals))] = percentDecode(param.substr(equals + 1));
            }
        }

        const auto encoding = req.headers.find("transfer-encoding");
        if (encoding != req.headers.end() && lower(encoding->second) != "identity")
            return 411;
        const auto length = req.headers.find("content-length");
        if (length != req.headers.end())
        {
            if (!parseNumber(length->second, req.length))
                return 400;
            req.hasLength = true;
            c.bodyLeft    = req.length;
        }
        return 0;
    }

    std::string param(const Request& req, const std::string& name)
    {
        const auto it = req.query.find(name);
        return it == req.query.end() ? "" : it->second;
    }

    bool numberParam(const Request& req, const std::string& name, uint64_t& out)
    {
        return parseNumber(param(req, name), out);
    }

    void servePage(Connection& c)
    {
        std::ifstream file(BRLS_ASSET("web/index.html"), std::ios::binary);
        if (!file)
            return respond(c, 500, "text/plain; charset=utf-8", "The page is missing from PortZone.");
        std::ostringstream text;
        text << file.rdbuf();
        respond(c, 200, "text/html; charset=utf-8", text.str());
    }

    json fileSpecJson(const FileSpec& spec)
    {
        return {{"path", spec.path}, {"note", spec.note}, {"match", spec.match}, {"markers", spec.markers}};
    }

    // Where the manual browser opens for a port: its game folder, else where its release went, else its .nro's folder,
    // else switch/<repository name>.
    std::string startFolder(const Port& port)
    {
        if (port.manifest && port.manifest->gameData)
            return sdShown(port.manifest->gameData->dir);
        if (const auto record = loadRecord(port.info.id); record && !record->target.empty())
            return sdShown(record->target);
        if (port.manifest && !port.manifest->binary.empty())
            return sdShown(port.manifest->binary.substr(0, port.manifest->binary.rfind('/')));
        return sdShown(port.defaultFolder());
    }

    void servePorts(Connection& c)
    {
        Server& s  = server();
        json ports = json::array();
        for (const Port& port : s.ports)
        {
            const PortState state = portState(port);
            const char* kind      = port.section() == Section::EasySetup ? "easy" :
                     port.section() == Section::PrepareOnPc             ? "prep" :
                     port.section() == Section::BuildOnPc               ? "pc" :
                                                                          "manual";
            json entry = {{"id", port.info.id}, {"title", port.info.title}, {"author", port.info.author},
                {"description", port.description()}, {"icon", port.iconUrl()}, {"platform", port.originalPlatform()},
                {"kind", kind}, {"installed", state.installed}, {"status", stateSummary(port, state)},
                {"good", stateGood(port, state)}, {"dir", startFolder(port)}, {"manualOnly", true},
                {"prepare", nullptr}, {"inputs", json::array()}};
            if (!port.manifest || !port.manifest->gameData)
            {
                ports.push_back(std::move(entry));
                continue;
            }
            const GameData& data  = *port.manifest->gameData;
            const DataCheck check = checkGameData(data);

            if (data.prepare)
            {
                json tool = nullptr;
                if (!data.prepare->toolName.empty())
                    tool = {{"name", data.prepare->toolName}, {"url", data.prepare->toolUrl}};
                entry["prepare"] = {{"where", data.prepare->where}, {"summary", data.prepare->summary}, {"tool", tool}};
            }

            for (const Input& input : data.inputs)
            {
                bool met = false;
                for (const InputCheck& inputCheck : check.inputs)
                    if (inputCheck.input == &input)
                        met = inputCheck.met;
                json options = json::array();
                for (const Option& option : input.options)
                {
                    json files = json::array();
                    for (const FileSpec& spec : option.files)
                        files.push_back(fileSpecJson(spec));
                    options.push_back({{"label", option.label}, {"files", files}});
                }
                entry["inputs"].push_back({{"id", input.id}, {"label", input.label}, {"required", input.required},
                    {"met", met}, {"options", options}});
            }
            entry["manualOnly"] = false;
            ports.push_back(std::move(entry));
        }
        respondJson(c, 200, {{"focus", s.focus}, {"ports", ports}});
    }

    uint64_t placedSize(const matcher::Placement& placement)
    {
        return placement.size ? *placement.size : server().sources[placement.source].size;
    }

    // filesTotal/bytesTotal from the matches being sent.
    void recount(Status& status)
    {
        const Server& s    = server();
        status.filesTotal  = 0;
        status.bytesTotal  = 0;
        for (size_t index : s.counted)
        {
            status.filesTotal += s.matches[index].files.size();
            for (const auto& placement : s.matches[index].files)
                status.bytesTotal += placedSize(placement);
        }
    }

    void servePlan(Connection& c, const Request& req)
    {
        Server& s = server();
        if (!req.hasLength)
            return respondError(c, 411, "Content-Length is required.");
        if (req.length > PLAN_LIMIT)
            return respondError(c, 413, "Too many files at once.");
        std::string body;
        if (!readWholeBody(c, body))
            return;

        std::vector<matcher::Source> sources;
        try
        {
            const json parsed = json::parse(body);
            for (const json& file : parsed.at("files"))
                sources.push_back({file.at("path").get<std::string>(), file.at("size").get<uint64_t>()});
        }
        catch (const json::exception&)
        {
            return respondError(c, 400, "The file list could not be read.");
        }

        const std::string portId = param(req, "port");
        std::vector<const Port*> ports;
        for (const Port& port : s.ports)
            if (port.manifest && port.manifest->gameData && (portId.empty() || port.info.id == portId))
                ports.push_back(&port);
        if (!portId.empty() && ports.empty())
            return respondError(c, 400, "Unknown port " + portId + ".");

        update([&](Status& status) { status.phase = "Planning (" + std::to_string(sources.size()) + " files offered)"; });
        matcher::Plan plan = matcher::plan(sources, ports, param(req, "input"));

        s.sources = std::move(sources);
        s.matches = std::move(plan.matches);
        s.planned = true;
        ++s.planNumber;
        s.counted.clear();
        s.received.clear();
        s.failed = 0;

        json matches = json::array();
        size_t selected = 0;
        for (size_t i = 0; i < s.matches.size(); ++i)
        {
            const matcher::Match& match = s.matches[i];
            if (match.selected)
            {
                s.counted.insert(i);
                ++selected;
            }
            json files = json::array();
            for (const auto& placement : match.files)
                files.push_back({{"source", placement.source}, {"dest", placement.dest}, {"size", placedSize(placement)}});
            matches.push_back({{"id", i}, {"port", match.portId}, {"portTitle", match.portTitle}, {"input", match.inputId},
                {"inputLabel", match.inputLabel}, {"option", match.optionLabel}, {"required", match.required},
                {"alreadyMet", match.alreadyMet}, {"group", match.group}, {"selected", match.selected},
                {"bytes", match.bytes}, {"files", files}});
        }

        update([&](Status& status) {
            status.filesDone = 0;
            status.bytesDone = 0;
            recount(status);
            status.phase = "Plan: " + std::to_string(selected) + (selected == 1 ? " item, " : " items, ") +
                           formatSize(status.bytesTotal);
        });
        respondJson(c, 200, {{"plan", s.planNumber}, {"unused", plan.unused}, {"matches", matches}});
    }

    // The browser sends a match the suggestion did not pick: it replaces the untouched alternatives in its group.
    void countMatch(size_t index)
    {
        Server& s = server();
        if (s.counted.count(index))
            return;
        const int group = s.matches[index].group;
        if (group >= 0)
            for (auto it = s.counted.begin(); it != s.counted.end();)
            {
                const size_t other = *it;
                const bool touched = std::any_of(s.received.begin(), s.received.end(),
                    [&](const std::pair<size_t, size_t>& done) { return done.first == other; });
                if (s.matches[other].group == group && !touched)
                    it = s.counted.erase(it);
                else
                    ++it;
            }
        s.counted.insert(index);
    }

    void rejectFile(Connection& c, int code, const std::string& name, const std::string& why, const std::string& message)
    {
        server().failed++;
        update([&](Status& status) {
            status.currentFile.clear();
            status.currentDone = status.currentTotal = 0;
            status.bytesPerSecond                    = 0;
            addLog(status, "Rejected " + name + ": " + why);
        });
        respondError(c, code, message);
    }

    // Receives the request's body into the .part file, checked against `sha256` when that is not empty. True when
    // the whole file is there; otherwise the .part is gone and the browser has its answer (none when it left).
    bool receiveFile(Connection& c, const Request& req, const std::string& name, const std::vector<std::string>& sha256)
    {
        Server& s           = server();
        const fs::path part = partPath();
        std::error_code ec;
        fs::create_directories(part.parent_path(), ec);
        FILE* out = std::fopen(part.string().c_str(), "wb");
        if (!out)
        {
            rejectFile(c, 500, name, "could not write", "PortZone could not write to the SD card.");
            return false;
        }
        std::vector<char> writeBuffer(WRITE_BUFFER);
        std::setvbuf(out, writeBuffer.data(), _IOFBF, writeBuffer.size());

        update([&](Status& status) {
            status.phase        = "Receiving " + name;
            status.currentFile  = name;
            status.currentDone  = 0;
            status.currentTotal = req.length;
        });

        const bool hashing = !sha256.empty();
        picosha2::hash256_one_by_one hasher;
        std::vector<char> buffer(RECV_BUFFER);
        uint64_t done       = 0;
        bool writeFailed    = false;
        auto windowStart    = Clock::now();
        auto lastShown      = windowStart;
        uint64_t windowDone = 0;
        double speed        = 0;
        while (done < req.length)
        {
            const size_t got = readBody(c, buffer.data(), buffer.size());
            if (got == 0)
                break;
            if (std::fwrite(buffer.data(), 1, got, out) != got)
            {
                writeFailed = true;
                break;
            }
            if (hashing)
                hasher.process(buffer.begin(), buffer.begin() + std::ptrdiff_t(got));
            done += got;
            windowDone += got;

            const auto now      = Clock::now();
            const double window = std::chrono::duration<double>(now - windowStart).count();
            if (window >= 1.0)
            {
                speed       = double(windowDone) / window;
                windowStart = now;
                windowDone  = 0;
            }
            if (now - lastShown >= std::chrono::milliseconds(200))
            {
                lastShown = now;
                update([&](Status& status) {
                    status.currentDone    = done;
                    status.bytesPerSecond = speed;
                });
            }
        }
        const bool closed = std::fclose(out) == 0;

        if (done < req.length || writeFailed || !closed)
        {
            fs::remove(part, ec);
            if (writeFailed || !closed)
            {
                rejectFile(c, 500, name, "could not write (SD card full?)",
                    "PortZone could not write " + name + ". Is the SD card full?");
                return false;
            }
            s.failed++;
            update([&](Status& status) {
                status.currentFile.clear();
                status.currentDone = status.currentTotal = 0;
                status.bytesPerSecond                    = 0;
                status.phase = s.stopping ? "Stopped" : "Waiting for a browser";
                addLog(status, "Stopped receiving " + name);
            });
            return false;
        }

        if (hashing)
        {
            hasher.finish();
            const std::string hash = picosha2::get_hash_hex_string(hasher);
            if (std::find(sha256.begin(), sha256.end(), hash) == sha256.end())
            {
                fs::remove(part, ec);
                rejectFile(c, 409, name, "not a known version", "not a known version of " + name);
                return false;
            }
        }
        return true;
    }

    // Moves the received file to `to` and counts it; replies to the browser either way.
    void finishFile(Connection& c, const std::string& name, uint64_t size, const fs::path& to, const std::string& shown,
        bool counts)
    {
        std::error_code ec;
        const std::string error = moveFile(partPath(), to);
        if (!error.empty())
        {
            fs::remove(partPath(), ec);
            return rejectFile(c, 500, name, error, "PortZone " + error + ".");
        }
        update([&](Status& status) {
            recount(status);
            if (counts)
            {
                status.filesDone++;
                status.bytesDone += size;
                status.filesTotal = std::max(status.filesTotal, status.filesDone);
                status.bytesTotal = std::max(status.bytesTotal, status.bytesDone);
            }
            status.currentFile.clear();
            status.currentDone = status.currentTotal = 0;
            status.bytesPerSecond                    = 0;
            addLog(status, "Received " + name + " \u2192 " + shown);
        });
        respondJson(c, 200, {{"ok", true}});
    }

    void serveFile(Connection& c, const Request& req)
    {
        Server& s = server();
        uint64_t planNumber, matchIndex, fileIndex;
        if (!numberParam(req, "plan", planNumber) || !numberParam(req, "match", matchIndex) ||
            !numberParam(req, "file", fileIndex))
            return respondError(c, 400, "plan, match and file are required.");
        if (!s.planned || planNumber != s.planNumber)
            return respondError(c, 410, "The list of files changed. Drop the files again.");
        if (matchIndex >= s.matches.size() || fileIndex >= s.matches[matchIndex].files.size())
            return respondError(c, 400, "No such file.");
        if (!req.hasLength)
            return respondError(c, 411, "Content-Length is required.");

        const matcher::Match& match         = s.matches[matchIndex];
        const matcher::Placement& placement = match.files[fileIndex];
        const matcher::Source& source       = s.sources[placement.source];
        const std::string name              = fileName(source.path);
        const std::string folder = fs::path(sdShown(match.gameDir)).filename().string();
        const std::string shown  = folder.empty() ? placement.dest : folder + "/" + placement.dest;

        if (req.length != source.size || (placement.size && *placement.size != req.length))
            return rejectFile(c, 409, name, "wrong size", "wrong size: " + name + " is not the expected file");
        if (!receiveFile(c, req, name, placement.sha256))
            return;

        const bool first = s.received.insert({size_t(matchIndex), size_t(fileIndex)}).second;
        countMatch(size_t(matchIndex));
        finishFile(c, name, req.length, gamefiles::destination(match, placement), shown, first);
    }

    // An SD-relative path the manual browser may use ("" is the root), as `parts`. False outside the SD card, in the
    // system's and CFW's folders and in PortZone's own data.
    bool manualPath(const std::string& path, std::vector<std::string>& parts)
    {
        if (path.find('\\') != std::string::npos || path.find(':') != std::string::npos)
            return false;
        std::istringstream in(path);
        std::string part;
        while (std::getline(in, part, '/'))
        {
            if (part.empty())
                continue;
            if (part == "." || part == ".." ||
                std::any_of(part.begin(), part.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x20; }))
                return false;
            parts.push_back(part);
        }
        if (!parts.empty() && gamefiles::isSystemFolder(parts[0]))
            return false;
        return !(parts.size() >= 2 && lower(parts[0]) == "config" && lower(parts[1]) == "portzone");
    }

    fs::path onCard(const std::vector<std::string>& parts)
    {
        fs::path out = config().sdRoot;
        for (const auto& part : parts)
            out /= part;
        return out;
    }

    std::string joined(const std::vector<std::string>& parts)
    {
        std::string out;
        for (const auto& part : parts)
            out += (out.empty() ? "" : "/") + part;
        return out;
    }

    void serveList(Connection& c, const Request& req)
    {
        constexpr size_t MAX_ENTRIES = 5000;
        std::vector<std::string> parts;
        if (!manualPath(param(req, "path"), parts))
            return respondError(c, 403, "PortZone doesn't allow changes there.");
        const fs::path dir = onCard(parts);
        std::error_code ec;
        const bool exists = fs::is_directory(dir, ec);

        std::vector<std::string> folders;
        std::vector<std::pair<std::string, uint64_t>> files;
        if (exists)
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end && folders.size() + files.size() < MAX_ENTRIES;
                 it.increment(ec))
            {
                const std::string name = it->path().filename().string();
                std::error_code typeError;
                if (it->is_directory(typeError))
                {
                    // Folders the browser may not open are not shown.
                    std::vector<std::string> child = parts, checked;
                    child.push_back(name);
                    if (manualPath(joined(child), checked))
                        folders.push_back(name);
                }
                else if (it->is_regular_file(typeError))
                    files.emplace_back(name, it->file_size(typeError));
            }
        const auto byName = [](const std::string& a, const std::string& b) { return lower(a) < lower(b); };
        std::sort(folders.begin(), folders.end(), byName);
        std::sort(files.begin(), files.end(), [&](const auto& a, const auto& b) { return byName(a.first, b.first); });

        json fileList = json::array();
        for (const auto& [name, size] : files)
            fileList.push_back({{"name", name}, {"size", size}});
        respondJson(c, 200, {{"path", joined(parts)}, {"exists", exists}, {"folders", folders}, {"files", fileList}});
    }

    void serveMkdir(Connection& c, const Request& req)
    {
        std::vector<std::string> parts;
        if (!manualPath(param(req, "path"), parts) || parts.empty())
            return respondError(c, 403, "PortZone doesn't allow changes there.");
        const fs::path dir = onCard(parts);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!fs::is_directory(dir, ec))
            return respondError(c, 409, "Could not make the folder " + joined(parts) + ".");
        update([&](Status& status) { addLog(status, "Made the folder " + joined(parts)); });
        respondJson(c, 200, {{"ok", true}});
    }

    void servePut(Connection& c, const Request& req)
    {
        std::vector<std::string> parts;
        if (!manualPath(param(req, "path"), parts) || parts.empty())
            return respondError(c, 403, "PortZone doesn't allow changes there.");
        if (!req.hasLength)
            return respondError(c, 411, "Content-Length is required.");
        const fs::path to = onCard(parts);
        std::error_code ec;
        if (fs::is_directory(to, ec))
            return respondError(c, 409, "A folder is already called " + parts.back() + ".");
        if (receiveFile(c, req, parts.back(), {}))
            finishFile(c, parts.back(), req.length, to, joined(parts), true);
    }

    void serveDone(Connection& c)
    {
        const Server& s = server();
        update([&](Status& status) {
            status.filesTotal = status.filesDone;
            status.bytesTotal = status.bytesDone;
            status.phase      = "Done: " + std::to_string(status.filesDone) + (status.filesDone == 1 ? " file" : " files");
            if (s.failed > 0)
                status.phase += ", " + std::to_string(s.failed) + (s.failed == 1 ? " error" : " errors");
            addLog(status, status.phase);
        });
        respondJson(c, 200, {{"ok", true}});
    }

    void handle(Connection& c)
    {
        Request req;
        const int headError = readHead(c, req);
        if (headError < 0)
            return;
        if (headError > 0)
        {
            c.bodyLeft = 0;
            return respondError(c, headError, reason(headError));
        }

        if (req.path == "/")
        {
            if (req.method != "GET")
                return respondError(c, 405, "Only GET here.");
            return servePage(c);
        }
        if (req.path.compare(0, 5, "/api/") != 0)
            return respondError(c, 404, "Not found.");
        if (param(req, "k") != server().key)
            return respondError(c, 403, "Open the address PortZone shows, with its key.");

        if (req.path == "/api/ports" && req.method == "GET")
            servePorts(c);
        else if (req.path == "/api/plan" && req.method == "POST")
            servePlan(c, req);
        else if (req.path == "/api/file" && req.method == "PUT")
            serveFile(c, req);
        else if (req.path == "/api/list" && req.method == "GET")
            serveList(c, req);
        else if (req.path == "/api/mkdir" && req.method == "POST")
            serveMkdir(c, req);
        else if (req.path == "/api/put" && req.method == "PUT")
            servePut(c, req);
        else if (req.path == "/api/done" && req.method == "POST")
            serveDone(c);
        else
            respondError(c, 404, "Not found.");
    }

    void serve()
    {
        Server& s = server();
        while (!s.stopping)
        {
            pollfd p{s.listenFd, POLLIN, 0};
            if (poll(&p, 1, POLL_MS) <= 0 || !(p.revents & POLLIN))
                continue;
            Connection c;
            c.fd = accept(s.listenFd, nullptr, nullptr);
            if (c.fd < 0)
                continue;
            try
            {
                handle(c);
            }
            catch (const std::exception& e)
            {
                brls::Logger::error("transfer: {}", e.what());
                respondError(c, 500, e.what());
            }
            drain(c);
            close(c.fd);
        }
    }

    // A socket listening on the first free port of 8080..8089, or -1.
    int listenSocket(int& port)
    {
        for (port = FIRST_PORT; port <= LAST_PORT; ++port)
        {
            const int fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
                return -1;
            int yes = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            sockaddr_in address{};
            address.sin_family      = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_ANY);
            address.sin_port        = htons(uint16_t(port));
            if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(fd, 4) == 0)
                return fd;
            close(fd);
        }
        return -1;
    }

} // namespace

bool start(std::vector<Port> ports, std::string focusPort)
{
    stop();
    Server& s = server();
    s.ports   = std::move(ports);
    s.focus   = std::move(focusPort);
    s.key     = makeKey();
    s.planned = false;
    s.sources.clear();
    s.matches.clear();
    s.counted.clear();
    s.received.clear();
    s.failed = 0;

    const auto fail = [&](const std::string& error) {
        update([&](Status& status) {
            const unsigned generation = status.generation;
            status                    = Status();
            status.generation         = generation;
            status.error              = error;
        });
        return false;
    };

    const std::string ip = brls::Application::getPlatform()->getIpAddress();
    if (ip.empty() || ip == "-" || ip == "0.0.0.0")
        return fail(NO_NETWORK);

    int port   = 0;
    s.listenFd = listenSocket(port);
    if (s.listenFd < 0)
        return fail("No free network port (8080 to 8089) to receive files on.");

    update([&](Status& status) {
        const unsigned generation = status.generation;
        status                    = Status();
        status.generation         = generation;
        status.running            = true;
        status.address            = "http://" + ip + ":" + std::to_string(port) + "/?k=" + s.key;
        status.phase              = "Waiting for a browser";
    });
    s.stopping = false;
    s.thread   = std::thread(serve);
    return true;
}

void stop()
{
    Server& s  = server();
    s.stopping = true;
    if (s.thread.joinable())
        s.thread.join();
    if (s.listenFd >= 0)
    {
        close(s.listenFd);
        s.listenFd = -1;
    }
    std::error_code ec;
    fs::remove(partPath(), ec);
    update([&](Status& status) {
        status.running = false;
        status.currentFile.clear();
        status.currentDone = status.currentTotal = 0;
        status.bytesPerSecond                    = 0;
    });
}

Status status()
{
    Server& s = server();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.status;
}

} // namespace pz::transfer
