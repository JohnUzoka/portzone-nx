#pragma once

#include <string>
#include <vector>

namespace pz
{

// A language PortZone's app text comes in: its borealis locale (resources/i18n/<code>/) and its own name.
struct Language
{
    const char* code;
    const char* name;
};

// English first, then the translations.
const std::vector<Language>& languages();

// The language to show for `tag` (a console or PC language such as "es-419", "zh-CN", "pt", "fr_CA.UTF-8"): an exact
// match, else its base language, else "" when PortZone has no translation.
std::string supportedLanguage(const std::string& tag);

// The console's language (the PC's on a PC build), as its system names it.
std::string systemLanguage();

// What PortZone shows: the player's choice in About, else the console's language when translated, else English.
std::string appLanguage(const std::string& chosen);

} // namespace pz
