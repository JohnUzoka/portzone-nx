#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "catalog.hpp"
#include "state.hpp"
#include "ui/widgets.hpp"

namespace pz::ui
{

// A port's card in a list: its status line is filled in once the list has read the port's state (in the background:
// reading the SD card for every card would hold up the controls while a tab opens).
class PortCard
{
  public:
    explicit PortCard(std::string id)
        : id(std::move(id))
    {
    }
    virtual ~PortCard() = default;
    virtual void showState(const Port& port, const PortState& state) = 0;

    const std::string id;
};

// The port list. Filters (kind, system the game came from) and the card/grid view live in one menu, opened with
// the Filter and view button or X; a line above the list says what is shown. `installedOnly` lists installed ports.
class BrowseView : public brls::Box
{
  public:
    explicit BrowseView(bool installedOnly);
    ~BrowseView() override;
    // Shown again (another tab, a game's page): the banner, and what installs changed in the list.
    void willAppear(bool resetState) override;

    // The update check for installed games finished: every Browse and Installed view on screen redraws.
    static void portUpdatesFound();

  private:
    void apply(); // after a change: the summary line and the list
    void fillBanner();
    void fillPortUpdates();
    void fillList();
    void addPage();               // views for the next ports of `shown`
    void readStates(size_t from); // the states of cards[from...], read in the background
    std::vector<std::string> wanted() const; // the ports the filters and search let through, in order
    void openFilters();
    void openSearch();
    void chooseKind();
    void chooseSystem();
    void chooseConsole();
    void chooseSort();
    void chooseView();

    bool installedOnly;
    int filter = 0;           // 0 all, then 1 + Section
    std::string systemFilter; // "" for every system
    std::string consoleFilter; // "" for every console, else "switch" or "switch2" (a port's runs_on)
    std::string search;       // "" shows every port
    brls::Box* banner           = nullptr; // "PortZone vX is available", installed games with updates
    brls::Label* summary        = nullptr;
    brls::Button* searchButton  = nullptr;
    brls::Button* filtersButton = nullptr;
    brls::Box* list             = nullptr;
    // The ports the filters and search let through, in order. Views are built a page at a time: reaching the last
    // ones built brings the next page, so a long list starts at once.
    std::vector<std::string> shown;
    size_t built = 0;
    std::vector<brls::View*> lastRow; // grid: the last row built, for moving up and down
    std::vector<PortCard*> cards;     // every card built, in order (the views belong to `list`)
    unsigned generation = 0;          // bumped when the list is rebuilt: states read for the old one are dropped
    std::chrono::steady_clock::time_point statesRead; // when the cards' states were last read
    AliveFlag alive             = std::make_shared<bool>(true);
};

// A port as a wide card: icon, title, author, description, status. Opens the port's page.
class PortRow : public CulledBox, public PortCard
{
  public:
    explicit PortRow(const Port& port);
    ~PortRow() override;
    void showState(const Port& port, const PortState& state) override;

  private:
    brls::Box* statusSlot = nullptr;
    AliveFlag alive       = std::make_shared<bool>(true);
};

// A port as a grid tile: big icon, title, status. Opens the port's page.
class PortTile : public brls::Box, public PortCard
{
  public:
    PortTile(const Port& port, float width);
    ~PortTile() override;
    void showState(const Port& port, const PortState& state) override;

  private:
    brls::Label* status = nullptr;
    AliveFlag alive     = std::make_shared<bool>(true);
};

} // namespace pz::ui
