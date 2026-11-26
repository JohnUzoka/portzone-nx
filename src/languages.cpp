#include "languages.hpp"

#include <cstdlib>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace pz
{

const std::vector<Language>& languages()
{
    static const std::vector<Language> list = {
        { "en-US", "English" },
        { "es", "Español" },
        { "fr", "Français" },
        { "de", "Deutsch" },
        { "it", "Italiano" },
        { "pt-BR", "Português (Brasil)" },
        { "ja", "日本語" },
        { "zh-Hans", "简体中文" },
    };
    return list;
}

std::string supportedLanguage(const std::string& tag)
{
    // "fr_CA.UTF-8" (a PC's LANG) reads as "fr-CA".
    std::string clean = tag.substr(0, tag.find('.'));
    for (char& c : clean)
        if (c == '_')
            c = '-';
    for (const Language& language : languages())
        if (clean == language.code)
            return language.code;

    // Variants of a translated language: Latin American Spanish, Canadian French, British English, European
    // Portuguese, the Simplified Chinese codes.
    if (clean == "zh-CN" || clean == "zh-SG")
        return "zh-Hans";
    const std::string base = clean.substr(0, clean.find('-'));
    if (base == "en")
        return "en-US";
    if (base == "pt")
        return "pt-BR";
    for (const Language& language : languages())
        if (base == language.code)
            return language.code;
    return "";
}

std::string systemLanguage()
{
#ifdef __SWITCH__
    // The set service, before borealis starts it: libnx counts the users, so borealis' own start is unaffected.
    std::string tag;
    if (R_SUCCEEDED(setInitialize()))
    {
        uint64_t code = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&code)))
            tag = std::string(reinterpret_cast<const char*>(&code), strnlen(reinterpret_cast<const char*>(&code), sizeof(code)));
        setExit();
    }
    return tag;
#else
    for (const char* variable : { "LC_ALL", "LC_MESSAGES", "LANG" })
        if (const char* value = std::getenv(variable); value && *value)
            return value;
    return "";
#endif
}

std::string appLanguage(const std::string& chosen)
{
    if (!chosen.empty())
        if (const std::string language = supportedLanguage(chosen); !language.empty())
            return language;
    const std::string system = supportedLanguage(systemLanguage());
    return system.empty() ? "en-US" : system;
}

} // namespace pz
