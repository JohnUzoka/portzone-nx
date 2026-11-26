#pragma once

#include <memory>
#include <optional>
#include <string>

#include <borealis.hpp>

#include "github.hpp"
#include "installer.hpp"
#include "ui/widgets.hpp"

namespace pz
{
struct Port;
}

namespace pz::ui
{

// One port: what it needs, where files go, install/update/remove; README and release files for manual ports.
class PortActivity : public brls::Activity
{
  public:
    explicit PortActivity(std::string id);
    ~PortActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;
    // Back from Send files or Find on SD card: the checklist may have changed.
    void onResume() override { refresh(); }

  private:
    const Port& port() const;
    // Whether this port's prereleases are wanted: its portzone.json says so, or the player turned test builds on.
    bool testBuilds() const;

    void refresh();
    void fillStatus(brls::Box* box);
    void fillNeeds(brls::Box* box);
    void fillLayout(brls::Box* box);
    void fillReleaseFiles(brls::Box* box);
    void fillReadme(brls::Box* box);

    void install(InstallPlan plan);
    void confirmRemove();
    void watch(std::shared_ptr<Job> job);
    void poll(std::shared_ptr<Job> watched);

    std::string id;
    AliveFlag alive = std::make_shared<bool>(true);

    brls::Box* intro         = nullptr; // stable focus target while other boxes rebuild
    brls::Box* statusBox     = nullptr;
    brls::Box* needsBox      = nullptr;
    brls::Box* layoutBox     = nullptr;
    brls::Box* releaseBox    = nullptr;
    brls::Box* readmeBox     = nullptr;
    brls::Label* progress    = nullptr;

    std::optional<github::Releases> releases;
    std::optional<github::Readme> readme;
    std::shared_ptr<Job> job;
};

} // namespace pz::ui
