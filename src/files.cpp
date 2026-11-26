#include "files.hpp"

#include <cstdio>
#include <fstream>

#include <picosha2.h>

namespace pz
{

namespace
{

    std::vector<std::string> split(const std::string& relative)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < relative.size())
        {
            size_t end = relative.find('/', start);
            if (end == std::string::npos)
                end = relative.size();
            if (end > start)
                parts.push_back(relative.substr(start, end - start));
            start = end + 1;
        }
        return parts;
    }

    bool isGlob(const std::string& part) { return part.find_first_of("*?") != std::string::npos; }

    // The entry of `dir` named `name`, ignoring case; a glob takes the first match in name order.
    std::optional<fs::path> findEntry(const fs::path& dir, const std::string& name)
    {
        std::error_code ec;
        if (!isGlob(name))
        {
            fs::path exact = dir / name;
            if (fs::exists(exact, ec))
                return exact;
        }
        std::optional<fs::path> best;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string entry = it->path().filename().string();
            if (isGlob(name) ? globMatch(name, entry) : lower(entry) == lower(name))
                if (!best || entry < best->filename().string())
                    best = it->path();
        }
        return best;
    }

    bool folderHasFiles(const fs::path& folder)
    {
        std::error_code ec;
        fs::directory_iterator it(folder, ec);
        return !ec && it != fs::directory_iterator();
    }

    FileCheck checkFile(const fs::path& dir, const FileSpec& spec)
    {
        FileCheck check;
        check.spec = &spec;
        const auto found = findPath(dir, spec.path);
        if (!found)
            return check;

        std::error_code ec;
        if (spec.isFolder())
        {
            if (!fs::is_directory(*found, ec))
                return check;
            for (const auto& marker : spec.markers)
                if (!findPath(*found, marker))
                {
                    check.detail = "no " + marker + " inside";
                    return check;
                }
            if (!spec.match.empty())
            {
                const size_t count = countMatches(*found, spec.match);
                check.found        = count > 0;
                check.detail       = count == 1 ? "1 file" : std::to_string(count) + " files";
            }
            else
                check.found = !spec.markers.empty() || folderHasFiles(*found);
            return check;
        }

        if (!fs::is_regular_file(*found, ec))
            return check;
        const uint64_t size = fs::file_size(*found, ec);
        if (spec.size && size != *spec.size)
        {
            check.detail = "wrong size: " + formatSize(size);
            return check;
        }
        if (!spec.sha256.empty() && size <= HASH_LIMIT)
        {
            const std::string hash = sha256Of(*found);
            bool known             = false;
            for (const auto& accepted : spec.sha256)
                known |= lower(accepted) == hash;
            if (!known)
            {
                check.detail = "not a known version";
                return check;
            }
        }
        check.found = true;
        if (isGlob(spec.path))
            check.detail = found->filename().string();
        return check;
    }

} // namespace

std::string sha256Of(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    picosha2::hash256_one_by_one hasher;
    std::vector<char> buffer(1 << 20);
    while (file)
    {
        file.read(buffer.data(), std::streamsize(buffer.size()));
        hasher.process(buffer.begin(), buffer.begin() + file.gcount());
    }
    hasher.finish();
    return picosha2::get_hash_hex_string(hasher);
}

std::string moveFile(const fs::path& from, const fs::path& to)
{
    std::error_code ec;
    // Already where it belongs (a port whose files sit at the card's root finds them in place): taking the old one
    // away below would delete it. Paths, not fs::equivalent: the SD card's FAT has no inode numbers to compare.
    if (lower(from.lexically_normal().string()) == lower(to.lexically_normal().string()))
        return "";
    fs::create_directories(to.parent_path(), ec);
    if (ec)
        return "could not make the folder " + to.parent_path().string();
    // Horizon's rename doesn't replace a file: take the old one away first.
    if (fs::exists(to, ec) && !fs::remove(to, ec))
        return "could not replace " + to.string();
    fs::rename(from, to, ec);
    if (!ec)
        return "";

    FILE* in  = std::fopen(from.string().c_str(), "rb");
    FILE* out = in ? std::fopen(to.string().c_str(), "wb") : nullptr;
    bool ok   = in && out;
    std::vector<char> buffer(1 << 20);
    while (ok)
    {
        const size_t got = std::fread(buffer.data(), 1, buffer.size(), in);
        if (got == 0)
        {
            ok = !std::ferror(in);
            break;
        }
        ok = std::fwrite(buffer.data(), 1, got, out) == got;
    }
    if (in)
        std::fclose(in);
    if (out && std::fclose(out) != 0)
        ok = false;
    if (!ok)
    {
        fs::remove(to, ec);
        return "could not write " + to.string();
    }
    fs::remove(from, ec);
    return "";
}

bool globMatch(const std::string& pattern, const std::string& name)
{
    // Iterative wildcard match with backtracking on the last *.
    const std::string p = lower(pattern), n = lower(name);
    size_t pi = 0, ni = 0, star = std::string::npos, mark = 0;
    while (ni < n.size())
    {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == n[ni]))
        {
            pi++;
            ni++;
        }
        else if (pi < p.size() && p[pi] == '*')
        {
            star = pi++;
            mark = ni;
        }
        else if (star != std::string::npos)
        {
            pi = star + 1;
            ni = ++mark;
        }
        else
            return false;
    }
    while (pi < p.size() && p[pi] == '*')
        pi++;
    return pi == p.size();
}

std::optional<fs::path> findPath(const fs::path& base, const std::string& relative)
{
    fs::path current = base;
    for (const auto& part : split(relative))
    {
        const auto next = findEntry(current, part);
        if (!next)
            return std::nullopt;
        current = *next;
    }
    return current;
}

size_t countMatches(const fs::path& folder, const std::string& pattern)
{
    size_t count = 0;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (globMatch(pattern, it->path().filename().string()))
            count++;
    return count;
}

bool present(const GameData& data, const std::string& relative)
{
    const auto found = findPath(sdPath(data.dir), relative);
    if (!found)
        return false;
    std::error_code ec;
    if (!relative.empty() && relative.back() == '/')
        return fs::is_directory(*found, ec) && folderHasFiles(*found);
    return true;
}

DataCheck checkGameData(const GameData& data)
{
    DataCheck result;
    const fs::path dir = sdPath(data.dir);

    if (data.prepare && !data.prepare->done.empty())
    {
        result.converted = true;
        for (const auto& done : data.prepare->done)
            result.converted &= present(data, done);
    }

    result.ready = true;
    for (const auto& input : data.inputs)
    {
        InputCheck check;
        check.input = &input;
        for (size_t i = 0; i < input.options.size(); i++)
        {
            OptionCheck option;
            option.option = &input.options[i];
            option.met    = true;
            for (const auto& file : input.options[i].files)
            {
                option.files.push_back(checkFile(dir, file));
                option.met &= option.files.back().found;
            }
            if (option.met && check.metOption < 0)
                check.metOption = int(i);
            check.options.push_back(std::move(option));
        }
        check.met = check.metOption >= 0 || (input.required && result.converted);
        if (input.required && !check.met)
        {
            result.ready = false;
            result.missing.push_back(input.label);
        }
        result.inputs.push_back(std::move(check));
    }
    return result;
}

} // namespace pz
