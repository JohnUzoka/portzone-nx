#pragma once

#include <optional>
#include <string>
#include <vector>

#include "model.hpp"

namespace pz
{

enum class Section
{
    EasySetup,   // portzone.json: install here, conversion (if any) on the Switch
    PrepareOnPc, // portzone.json: the .nro downloads here; a PC tool makes the game files
    BuildOnPc,   // portzone.json: a PC tool makes everything, the .nro included (no install.steps)
    Manual,      // no portzone.json: README and release files
};

// "Easy Setup", "Prepare on PC", "Build on PC", "Manual", in the player's language.
std::string sectionName(Section section);

struct Port
{
    IndexEntry info;
    std::optional<Manifest> manifest;
    nlohmann::json manifestJson; // as read, to tell when the repository's own copy has changed
    std::string manifestSource;  // where portzone.json came from
    std::string manifestError;   // a portzone.json that exists but cannot be used

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

    // Blocking, any thread: ports.json and manifests.json (every port's portzone.json, built by the store), from the
    // same place: the store on GitHub, else the copies saved last time, else the ones built into PortZone.
    static Loaded fetch();

    // Blocking, any thread: the portzone.json at the top of the port's repository now, when it differs from the one
    // `port` has (the store's index is rebuilt daily). Nothing when it is the same, can't be read, or the port uses
    // a --drafts file; a Port without a manifest when the repository's copy is gone.
    static std::optional<Port> fresher(const Port& port);

    // UI thread: makes a fetched list the current one.
    void apply(Loaded loaded);

    // UI thread: replaces one port (from fresher()).
    void replace(Port port);

    // UI thread: reads every port's portzone.json again, after config().console changed (Switch 2 builds).
    void reapply();

    std::vector<Port> ports;
    Source source = Source::Network;

    const Port* find(const std::string& id) const;
};

} // namespace pz
