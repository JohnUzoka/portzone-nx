#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace pz
{

enum class Section
{
    EasySetup, // portzone.json: install here, conversion (if any) on the Switch
    BuildOnPc, // portzone.json: a PC tool makes the game files
    Manual,    // no portzone.json: README and release files
};

struct Port
{
    IndexEntry info;
    std::optional<Manifest> manifest;
    std::string manifestSource; // where portzone.json came from
    std::string manifestError;  // a portzone.json that exists but cannot be used

    Section section() const;
    const std::string& description() const;
    // portzone.json's image in the repository, then ports.json's, then (icon only) the owner's GitHub avatar.
    std::string iconUrl() const;
    std::string bannerUrl() const; // "" when there is none
    // portzone.json's platform, then ports.json's.
    const std::string& originalPlatform() const;
    std::vector<std::string> runsOn() const; // ["switch"] when neither says
    bool runsHere() const;                   // runsOn() includes config().console
    // "sdmc:/switch/<repository name>": where a port goes when nothing says otherwise.
    std::string defaultFolder() const;
};

class Catalog
{
  public:
    enum class Source
    {
        Network, // ports.json as published
        Cache,   // offline: the copy saved last time
        BuiltIn, // offline on first start: the copy inside the app
    };

    struct Loaded
    {
        std::vector<Port> ports;
        Source source = Source::Network;
        std::string error; // set when there is no list at all
    };

    static Catalog& get();

    // Blocking, any thread: ports.json and every portzone.json.
    static Loaded fetch();

    // UI thread: makes a fetched list the current one.
    void apply(Loaded loaded);

    std::vector<Port> ports;
    Source source = Source::Network;

    const Port* find(const std::string& id) const;
};

} // namespace pz
