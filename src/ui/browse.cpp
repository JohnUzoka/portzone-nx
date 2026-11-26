#include "ui/browse.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "port_updates.hpp"
#include "settings.hpp"
#include "state.hpp"
#include "ui/port_page.hpp"
#include "ui/update_page.hpp"
#include "update.hpp"

namespace pz::ui
{

namespace
{

    constexpr int GRID_COLUMNS = 4;
    constexpr float GRID_TILE  = 176;
    constexpr float GRID_GAP   = 16;

    // "All", then every Section in its order.
    const char* FILTER_NAMES[] = { "All", "Easy Setup", "Prepare on PC", "Build on PC", "Manual" };
    constexpr int FILTER_COUNT = int(sizeof(FILTER_NAMES) / sizeof(FILTER_NAMES[0]));
    const char* SORT_NAMES[]   = { "Name A-Z", "Name Z-A", "System" };

    bool matches(const Port& port, int filter)
    {
        return filter == 0 || port.section() == Section(filter - 1);
    }

    // `query` is already lower case.
    bool searched(const Port& port, const std::string& query)
    {
        if (query.empty())
            return true;
        for (const std::string* field : { &port.info.title, &port.info.author, &port.info.id })
            if (lower(*field).find(query) != std::string::npos)
                return true;
        return lower(port.originalPlatform()).find(query) != std::string::npos;
    }

    std::string trimmed(const std::string& text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");
        return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    }

    void openPort(const std::string& id)
    {
        if (Catalog::get().find(id))
            brls::Application::pushActivity(new PortActivity(id));
    }

    // Browse and Installed views on screen, told when the update check finishes.
    std::set<BrowseView*>& liveViews()
    {
        static std::set<BrowseView*> views;
        return views;
    }

} // namespace

BrowseView::BrowseView(bool installedOnly)
    : brls::Box(brls::Axis::COLUMN)
    , installedOnly(installedOnly)
{
    setGrow(1);
    liveViews().insert(this);
    auto* frame = new brls::ScrollingFrame();
    frame->setGrow(1);
    // Each press moves to the next port and scrolls it into view. borealis' default ("natural") scrolls only while
    // the button is held, and takes focus back from a port that is not fully on screen yet.
    frame->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    content->setPadding(28, 48, 32, 48);

    // A new PortZone (until the player says Later) and new releases of installed games.
    banner = new brls::Box(brls::Axis::COLUMN);
    banner->setAlignItems(brls::AlignItems::STRETCH);
    content->addView(banner);
    fillBanner();

    // One line: what is shown, and the button (or X) that opens the filter menu.
    auto* toolbar = new brls::Box(brls::Axis::ROW);
    toolbar->setAlignItems(brls::AlignItems::CENTER);
    toolbar->setMarginBottom(4);
    summary = label("", 18, mutedColor());
    summary->setGrow(1);
    summary->setShrink(1);
    toolbar->addView(summary);
    searchButton = button("Search", [this] { openSearch(); });
    searchButton->setCornerRadius(20);
    toolbar->addView(searchButton);
    filtersButton = button("Filter and view", [this] { openFilters(); });
    filtersButton->setMarginRight(0);
    filtersButton->setCornerRadius(20);
    toolbar->addView(filtersButton);
    content->addView(toolbar);

    list = new brls::Box(brls::Axis::COLUMN);
    list->setAlignItems(brls::AlignItems::STRETCH);
    content->addView(list);

    frame->setContentView(content);
    addView(frame);
    registerAction("Filter", brls::BUTTON_X, [this](brls::View*) {
        openFilters();
        return true;
    });
    registerAction("Search", brls::BUTTON_BACK, [this](brls::View*) {
        openSearch();
        return true;
    });
#ifndef __SWITCH__
    // On a PC keyboard, borealis maps no key to X: the X key filters, F searches. Not on a Switch, where a USB
    // keyboard's keys would act twice (Ryujinx also hands the PC's keyboard to the console: X is its B button).
    registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_X), [this](brls::View*) {
        openFilters();
        return true;
    });
    registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_F), [this](brls::View*) {
        openSearch();
        return true;
    });
#endif
    apply();
}

void BrowseView::fillBanner()
{
    fillPortUpdates();
    const update::Status& status = update::current();
    if (installedOnly || !status.available || status.dismissed)
        return;
    auto* row = card(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setMarginBottom(16);
    row->setBorderColor(accentColor());
    row->setBorderThickness(2);
    auto* logo = new brls::Image();
    logo->setImageFromRes("icon/icon.png");
    logo->setWidth(48);
    logo->setHeight(48);
    logo->setMarginRight(16);
    row->addView(logo);
    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setGrow(1);
    text->setShrink(1);
    text->addView(textBlock("PortZone " + status.tag + " is available", 20));
    text->addView(textBlock("You have " PORTZONE_VERSION ". See what's new, and update in one step.", 16, mutedColor()));
    row->addView(text);
    row->addView(button("See update", [] { brls::Application::pushActivity(new UpdateActivity()); }, true));
    auto* later = button("Later", [this] {
        update::current().dismissed = true;
        rebuild(banner, filtersButton, [this](brls::Box*) { fillPortUpdates(); });
    });
    later->setMarginRight(0);
    row->addView(later);
    banner->addView(row);
}

void BrowseView::fillPortUpdates()
{
    std::vector<std::string> names;
    for (const auto& port : Catalog::get().ports)
        if (const std::string tag = portupdates::pending(port.info.id); !tag.empty())
            names.push_back(port.info.title + " " + tag);
    if (names.empty())
        return;
    auto* row = card(brls::Axis::COLUMN);
    row->setMarginBottom(16);
    row->setBorderColor(accentColor());
    row->setBorderThickness(2);
    std::string list;
    for (const auto& name : names)
        list += (list.empty() ? "" : "  ·  ") + name;
    row->addView(textBlock(names.size() == 1 ? "1 installed game has an update" :
                                               std::to_string(names.size()) + " installed games have updates",
        20));
    row->addView(textBlock(list, 16, accentColor()));
    row->addView(textBlock(installedOnly ? "Open a game to update it." : "They're marked below and in Installed. Open a game to update it.",
        16, mutedColor()));
    banner->addView(row);
}

void BrowseView::portUpdatesFound()
{
    for (BrowseView* view : liveViews())
    {
        rebuild(view->banner, view->filtersButton, [view](brls::Box*) { view->fillBanner(); });
        view->apply();
    }
}

BrowseView::~BrowseView()
{
    liveViews().erase(this);
    *alive = false;
}

void BrowseView::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    rebuild(banner, filtersButton, [this](brls::Box*) { fillBanner(); });
}

void BrowseView::apply()
{
    std::string text = installedOnly ? "Installed" : "All ports";
    if (filter != 0)
        text = installedOnly ? text + "  ·  " + FILTER_NAMES[filter] : FILTER_NAMES[filter];
    text += systemFilter.empty() ? "  ·  every system" : "  ·  from " + systemFilter;
    text += std::string("  ·  ") + SORT_NAMES[settings().sortOrder];
    summary->setText(text);
    searchButton->setText(search.empty() ? "Search" : "\"" + search + "\"");
    searchButton->setStyle(search.empty() ? &brls::BUTTONSTYLE_BORDERED : &brls::BUTTONSTYLE_PRIMARY);
    filtersButton->setStyle(filter != 0 || !systemFilter.empty() ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_BORDERED);
    rebuild(list, filtersButton, [this](brls::Box*) { fillList(); });
}

void BrowseView::openSearch()
{
    brls::Application::getImeManager()->openForText([this](std::string text) {
        search = trimmed(text);
        apply();
    }, "Search games", "A name, author or system; empty shows everything", 40, search);
}

void BrowseView::openFilters()
{
    const bool filtered = filter != 0 || !systemFilter.empty() || !search.empty();
    std::vector<std::string> items = {
        std::string("Kind: ") + FILTER_NAMES[filter],
        "System: " + (systemFilter.empty() ? std::string("All") : systemFilter),
        std::string("Sort: ") + SORT_NAMES[settings().sortOrder],
        std::string("View: ") + (settings().gridView ? "Grid" : "Cards"),
    };
    if (filtered)
        items.push_back("Show everything");

    // Each choice opens its own list once this one has closed.
    auto* menu = new brls::Dropdown("Filter and view", items, [](int) {}, 0, [this](int chosen) {
        if (chosen == 0)
            chooseKind();
        else if (chosen == 1)
            chooseSystem();
        else if (chosen == 2)
            chooseSort();
        else if (chosen == 3)
            chooseView();
        else if (chosen == 4)
        {
            filter = 0;
            systemFilter.clear();
            search.clear();
            apply();
        }
    });
    brls::Application::pushActivity(new brls::Activity(menu));
}

void BrowseView::chooseKind()
{
    std::vector<std::string> names;
    for (int i = 0; i < FILTER_COUNT; i++)
    {
        size_t count = 0;
        for (const auto& port : Catalog::get().ports)
            count += matches(port, i);
        names.push_back(std::string(FILTER_NAMES[i]) + "  " + std::to_string(count));
    }
    auto* dropdown = new brls::Dropdown("Kind", names, [](int) {}, filter, [this](int chosen) {
        if (chosen < 0 || chosen >= FILTER_COUNT)
            return;
        filter = chosen;
        apply();
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void BrowseView::chooseSystem()
{
    // The systems the ports came from, with how many of each.
    std::map<std::string, size_t> counts;
    for (const auto& port : Catalog::get().ports)
        if (!port.originalPlatform().empty())
            counts[port.originalPlatform()]++;
    std::vector<std::string> systems { "" };
    std::vector<std::string> names { "All systems  " + std::to_string(Catalog::get().ports.size()) };
    int selected = 0;
    for (const auto& [system, count] : counts)
    {
        if (system == systemFilter)
            selected = int(systems.size());
        systems.push_back(system);
        names.push_back(system + "  " + std::to_string(count));
    }
    auto* dropdown = new brls::Dropdown("System", names, [](int) {}, selected, [this, systems](int chosen) {
        if (chosen < 0 || chosen >= int(systems.size()))
            return;
        systemFilter = systems[size_t(chosen)];
        apply();
    });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void BrowseView::chooseSort()
{
    auto* dropdown = new brls::Dropdown("Sort", { SORT_NAMES[0], SORT_NAMES[1], SORT_NAMES[2] }, [](int) {},
        settings().sortOrder, [this](int chosen) {
            if (chosen < 0 || chosen > 2)
                return;
            settings().sortOrder = chosen;
            saveSettings();
            apply();
        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void BrowseView::chooseView()
{
    auto* dropdown = new brls::Dropdown("View", { "Cards", "Grid" }, [](int) {}, settings().gridView ? 1 : 0,
        [this](int chosen) {
            if (chosen < 0 || chosen > 1)
                return;
            settings().gridView = chosen == 1;
            saveSettings();
            apply();
        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void BrowseView::fillList()
{
    // The catalog is already A-Z by title.
    const std::string query = lower(search);
    std::vector<const Port*> ports;
    for (const auto& port : Catalog::get().ports)
        if (matches(port, filter) && (systemFilter.empty() || port.originalPlatform() == systemFilter) &&
            (!installedOnly || portState(port).installed) && searched(port, query))
            ports.push_back(&port);
    if (settings().sortOrder == 1)
        std::reverse(ports.begin(), ports.end());
    else if (settings().sortOrder == 2)
        std::stable_sort(ports.begin(), ports.end(), [](const Port* a, const Port* b) {
            const std::string& x = a->originalPlatform();
            const std::string& y = b->originalPlatform();
            return x.empty() != y.empty() ? y.empty() : lower(x) < lower(y); // ports with no system last
        });

    shown.clear();
    for (const Port* port : ports)
        shown.push_back(port->info.id);
    built = 0;
    lastRow.clear();

    if (shown.empty())
    {
        std::string message = installedOnly && query.empty() && filter == 0 && systemFilter.empty() ?
            "Nothing installed yet. Install a port from Browse." : "No ports match.";
        if (!search.empty())
            message = "No ports match \"" + search + "\". Press - to search again.";
        list->addView(textRow(message, 20, mutedColor()));
        return;
    }
    addPage();
}

void BrowseView::addPage()
{
    const bool grid  = settings().gridView;
    const size_t end = std::min(shown.size(), built + (grid ? GRID_COLUMNS * 8 : 24));
    std::vector<brls::View*> added;

    if (!grid)
        for (size_t i = built; i < end; i++)
        {
            if (const Port* port = Catalog::get().find(shown[i]))
            {
                auto* row = new PortRow(*port);
                list->addView(row);
                added.push_back(row);
            }
        }
    else
        // Rows of tiles; up and down keep to the same column.
        for (size_t i = built; i < end; i += GRID_COLUMNS)
        {
            auto* row = new CulledBox(brls::Axis::ROW);
            row->setMarginTop(GRID_GAP);
            std::vector<brls::View*> tiles;
            for (size_t j = i; j < std::min(end, i + GRID_COLUMNS); j++)
                if (const Port* port = Catalog::get().find(shown[j]))
                {
                    auto* tile = new PortTile(*port, GRID_TILE);
                    if (!tiles.empty())
                        tile->setMarginLeft(GRID_GAP);
                    row->addView(tile);
                    tiles.push_back(tile);
                }
            if (tiles.empty())
            {
                delete row;
                continue;
            }
            for (size_t c = 0; c < tiles.size(); c++)
                if (!lastRow.empty())
                    tiles[c]->setCustomNavigationRoute(brls::FocusDirection::UP, lastRow[std::min(c, lastRow.size() - 1)]);
            for (size_t c = 0; c < lastRow.size(); c++)
                lastRow[c]->setCustomNavigationRoute(brls::FocusDirection::DOWN, tiles[std::min(c, tiles.size() - 1)]);
            list->addView(row);
            lastRow = tiles;
            added.insert(added.end(), tiles.begin(), tiles.end());
        }

    built = end;
    if (built == shown.size())
        return;
    // Focus on the last two rows (or three cards) built brings the next page before the end is reached.
    const size_t near = grid ? GRID_COLUMNS * 2 : 3;
    for (size_t i = added.size() > near ? added.size() - near : 0; i < added.size(); i++)
        added[i]->getFocusEvent()->subscribe([this, end, alive = alive](brls::View*) {
            brls::sync([this, end, alive] {
                if (*alive && built == end)
                    addPage();
            });
        });
}

PortRow::PortRow(const Port& port)
    : CulledBox(brls::Axis::ROW)
    , id(port.info.id)
{
    setFocusable(true);
    setAlignItems(brls::AlignItems::CENTER);
    setPadding(16, 20, 16, 16);
    setMarginTop(14);
    setCornerRadius(14);
    setBackgroundColor(cardColor());

    auto* icon = remoteIcon(port.iconUrl(), port.info.title, port.info.id, 76, alive);
    icon->setMarginRight(18);
    addView(icon);

    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setGrow(1);
    text->setShrink(1);
    text->setMarginRight(16);
    text->addView(textBlock(port.info.title, 24));
    const std::string& system = port.originalPlatform();
    text->addView(textBlock("by " + port.info.author + (system.empty() ? "" : "  ·  " + system), 15, mutedColor()));
    auto* description = textBlock(port.description(), 17, mutedColor());
    description->setMarginTop(4);
    text->addView(description);
    addView(text);

    statusSlot = new brls::Box(brls::Axis::ROW);
    statusSlot->setShrink(0);
    addView(statusSlot);
    refresh();

    registerClickAction([id = id](brls::View*) {
        openPort(id);
        return true;
    });
    addGestureRecognizer(new brls::TapGestureRecognizer(this)); // a tap runs the A action, as on a brls::Button
}

PortRow::~PortRow() { *alive = false; }

void PortRow::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    refresh();
}

void PortRow::refresh()
{
    const Port* port = Catalog::get().find(id);
    if (!port)
        return;
    const PortState state = portState(*port);
    statusSlot->clearViews();
    if (const std::string tag = portupdates::pending(id); !tag.empty())
        statusSlot->addView(pill("Update: " + tag, accentColor()));
    else
        statusSlot->addView(pill(stateSummary(*port, state), stateColor(stateGood(*port, state), state.installed)));
}

PortTile::PortTile(const Port& port, float width)
    : brls::Box(brls::Axis::COLUMN)
    , id(port.info.id)
{
    setFocusable(true);
    setWidth(width);
    setAlignItems(brls::AlignItems::CENTER);
    setPadding(14, 12, 14, 12);
    setCornerRadius(16);
    setBackgroundColor(cardColor());

    const float iconSize = width - 40;
    addView(remoteIcon(port.iconUrl(), port.info.title, port.info.id, iconSize, alive));

    // Two lines at most, the same height on every tile so rows line up.
    auto* title = label(port.info.title, 16);
    title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    title->setVerticalAlign(brls::VerticalAlign::TOP);
    title->setWidth(width - 24);
    title->setHeight(46);
    title->setMarginTop(10);
    addView(title);

    status = label("", 14, mutedColor());
    status->setSingleLine(true);
    status->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    status->setWidth(width - 24);
    status->setMarginTop(4);
    addView(status);
    refresh();

    registerClickAction([id = id](brls::View*) {
        openPort(id);
        return true;
    });
    addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

PortTile::~PortTile() { *alive = false; }

void PortTile::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    refresh();
}

void PortTile::refresh()
{
    const Port* port = Catalog::get().find(id);
    if (!port)
        return;
    const PortState state = portState(*port);
    if (const std::string tag = portupdates::pending(id); !tag.empty())
    {
        status->setText("Update: " + tag);
        status->setTextColor(accentColor());
        return;
    }
    status->setText(stateShort(*port, state));
    status->setTextColor(stateColor(stateGood(*port, state), state.installed));
}

} // namespace pz::ui
