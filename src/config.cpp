#include "config.hpp"

#include <cstdio>
#include <cstring>

namespace pz
{

Config& config()
{
    static Config instance;
    return instance;
}

std::string parseArgs(int argc, char* argv[])
{
    Config& c = config();
#ifdef __SWITCH__
    c.sdRoot = "sdmc:/";
    // hbloader passes the .nro's own path; launchers may add arguments of their own, which PortZone ignores.
    if (argc > 0 && std::string(argv[0]).rfind("sdmc:/", 0) == 0 && endsWithNoCase(argv[0], ".nro"))
        c.selfPath = argv[0];
#else
    c.sdRoot = fs::current_path() / "sdmc";
    for (int i = 1; i < argc; i++)
    {
        const bool hasValue = i + 1 < argc;
        if (!std::strcmp(argv[i], "--index") && hasValue)
            c.indexUrl = argv[++i];
        else if (!std::strcmp(argv[i], "--sd") && hasValue)
            c.sdRoot = fs::absolute(argv[++i]);
        else if (!std::strcmp(argv[i], "--drafts") && hasValue)
            c.draftsDir = fs::absolute(argv[++i]);
        else if (!std::strcmp(argv[i], "--console") && hasValue)
        {
            c.console         = argv[++i];
            c.consoleFromArgs = true;
        }
        else if (!std::strcmp(argv[i], "--update-repo") && hasValue)
            c.updateRepo = argv[++i];
        else if (!std::strcmp(argv[i], "--self") && hasValue)
            c.selfPath = fs::absolute(argv[++i]);
        else if (!std::strcmp(argv[i], "-d") || !std::strcmp(argv[i], "-v"))
            continue; // borealis debug flags
        else
            return std::string("unknown argument: ") + argv[i] +
                "\nusage: portzone [--index URL|FILE] [--sd FOLDER] [--drafts FOLDER] [--console switch|switch2]"
                " [--update-repo OWNER/REPO] [--self FILE.nro]";
    }
#endif
    std::error_code ec;
    fs::create_directories(c.sdRoot, ec);
    fs::create_directories(dataDir(), ec);
    return "";
}

fs::path sdPath(const std::string& sdmcPath)
{
    std::string rest = sdShown(sdmcPath);
    while (!rest.empty() && rest.front() == '/')
        rest.erase(0, 1);
    return rest.empty() ? config().sdRoot : config().sdRoot / rest;
}

std::string sdShown(const std::string& sdmcPath)
{
    return sdmcPath.rfind("sdmc:/", 0) == 0 ? sdmcPath.substr(6) : sdmcPath;
}

fs::path dataDir() { return config().sdRoot / "config" / "portzone"; }

std::string lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return text;
}

bool endsWithNoCase(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() && lower(text.substr(text.size() - suffix.size())) == lower(suffix);
}

std::string formatSize(uint64_t bytes)
{
    const char* units[] = { "B", "KB", "MB", "GB" };
    double value = double(bytes);
    int unit     = 0;
    while (value >= 1024 && unit < 3)
    {
        value /= 1024;
        unit++;
    }
    char out[32];
    std::snprintf(out, sizeof(out), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return out;
}

} // namespace pz
