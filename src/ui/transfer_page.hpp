#pragma once

#include <string>

#include <borealis.hpp>

#include "ui/widgets.hpp"

namespace pz::ui
{

// Send files: serves a page on the local network that takes game files from a phone or PC while it is open.
class TransferActivity : public brls::Activity
{
  public:
    explicit TransferActivity(std::string focusPort = "");
    ~TransferActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    void start();
    void refresh();
    void fillStatus(brls::Box* box);
    void poll();

    std::string focusPort;
    brls::Box* intro      = nullptr; // stable focus target while the status box rebuilds
    brls::Label* address  = nullptr;
    brls::Box* statusBox  = nullptr;
    unsigned shown        = 0;
    AliveFlag alive       = std::make_shared<bool>(true);
};

} // namespace pz::ui
