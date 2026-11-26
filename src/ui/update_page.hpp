#pragma once

#include <memory>

#include <borealis.hpp>

#include "installer.hpp"
#include "ui/widgets.hpp"

namespace pz::ui
{

// PortZone's own updates: what's new, Update, progress, Restart. Checks GitHub when opened if nothing has yet.
class UpdateActivity : public brls::Activity
{
  public:
    ~UpdateActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    void check();
    void refresh();
    void fillStatus(brls::Box* box);
    void fillNotes(brls::Box* box);
    void startUpdate();
    void poll(std::shared_ptr<Job> watched);

    brls::Box* intro     = nullptr; // stable focus target while the other boxes rebuild
    brls::Box* statusBox = nullptr;
    brls::Box* notesBox  = nullptr;
    brls::Label* progress = nullptr;
    bool checking         = false;
    std::shared_ptr<Job> job;
    AliveFlag alive = std::make_shared<bool>(true);
};

} // namespace pz::ui
