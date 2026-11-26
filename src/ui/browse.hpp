#pragma once

#include <string>
#include <vector>

#include <borealis.hpp>

#include "catalog.hpp"
#include "ui/widgets.hpp"

namespace pz::ui
{

// The port list. Filters (kind, system the game came from) and the card/grid view live in one menu, opened with
// the Filter and view button or X; a line above the list says what is shown. `installedOnly` lists installed ports.
class BrowseView : public brls::Box
{
  public:
    explicit BrowseView(bool installedOnly);
    ~BrowseView() override;
    // Back from a game's page: an update there may have cleared the banner.
    void willAppear(bool resetState) override;

    // The update check for installed games finished: every Browse and Installed view on screen redraws.
    static void portUpdatesFound();

  private:
    void apply(); // after a change: the summary line and the list
    void fillBanner();
    void fillPortUpdates();
    void fillList();
    void openFilters();
    void openSearch();
    void chooseKind();
    void chooseSystem();
    void chooseSort();
    void chooseView();

    bool installedOnly;
    int filter = 0;           // 0 all, then 1 + Section
    std::string systemFilter; // "" for every system
    std::string search;       // "" shows every port
    brls::Box* banner           = nullptr; // "PortZone vX is available", installed games with updates
    brls::Label* summary        = nullptr;
    brls::Button* searchButton  = nullptr;
    brls::Button* filtersButton = nullptr;
    brls::Box* list             = nullptr;
    AliveFlag alive             = std::make_shared<bool>(true);
};

// A port as a wide card: icon, title, author, description, status. Opens the port's page.
class PortRow : public brls::Box
{
  public:
    explicit PortRow(const Port& port);
    ~PortRow() override;
    void willAppear(bool resetState) override;

  private:
    void refresh();

    std::string id;
    brls::Box* statusSlot = nullptr;
    AliveFlag alive       = std::make_shared<bool>(true);
};

// A port as a grid tile: big icon, title, status. Opens the port's page.
class PortTile : public brls::Box
{
  public:
    PortTile(const Port& port, float width);
    ~PortTile() override;
    void willAppear(bool resetState) override;

  private:
    void refresh();

    std::string id;
    brls::Label* status = nullptr;
    AliveFlag alive     = std::make_shared<bool>(true);
};

} // namespace pz::ui
