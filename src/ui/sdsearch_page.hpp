#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "catalog.hpp"
#include "installer.hpp"
#include "matcher.hpp"
#include "ui/widgets.hpp"

namespace pz::ui
{

// Find on SD card: looks for game files already on the SD card and moves the chosen ones into their ports' folders.
class SdSearchActivity : public brls::Activity
{
  public:
    explicit SdSearchActivity(std::string focusPort = "");
    ~SdSearchActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    void search();
    void pollSearch();
    void refresh();
    void fillResults(brls::Box* box);
    void fillActions(brls::Box* box);
    void toggle(size_t index);
    void move();
    void pollMove(std::shared_ptr<Job> watched);

    std::string focusPort;
    AliveFlag alive = std::make_shared<bool>(true);
    std::shared_ptr<std::atomic<bool>> cancel;
    std::shared_ptr<std::atomic<size_t>> seen;

    bool searching = false;
    std::vector<matcher::Source> sources;
    matcher::Plan plan;
    std::vector<bool> chosen;
    std::vector<brls::Label*> marks;
    std::shared_ptr<Job> job;

    brls::Box* intro       = nullptr; // stable focus target while the other boxes rebuild
    brls::Box* resultsBox  = nullptr;
    brls::Box* actionsBox  = nullptr;
    brls::Label* progress  = nullptr;
};

} // namespace pz::ui
