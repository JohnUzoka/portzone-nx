#pragma once

#include <borealis.hpp>

#include "catalog.hpp"
#include "ui/widgets.hpp"

namespace pz::ui
{

// The first screen: Browse, Installed and About in the sidebar. Y reloads the port list.
class HomeActivity : public brls::Activity
{
  public:
    ~HomeActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;
    // Back from a port's page: ports re-read the SD card.
    void onResume() override;

  private:
    void load();
    void showCatalog(int tab);
    // New releases of the games PortZone installed, after the port list is shown.
    void checkPortUpdates();
    // "Installed (2)" while games have updates.
    void markInstalledTab();
    void showError(const std::string& error);
    void chooseTheme();
    void recolorFrame();

    brls::AppletFrame* frame = nullptr;
    brls::TabFrame* tabs     = nullptr;
    brls::Box* root          = nullptr;
    brls::Box* parking       = nullptr; // holds focus while root is rebuilt
    bool loading             = false;
    AliveFlag alive          = std::make_shared<bool>(true);
};

} // namespace pz::ui
