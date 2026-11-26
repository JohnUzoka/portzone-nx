#include "ui/home.hpp"

#include "config.hpp"
#include "port_updates.hpp"
#include "settings.hpp"
#include "ui/browse.hpp"
#include "ui/sdsearch_page.hpp"
#include "ui/transfer_page.hpp"
#include "ui/update_page.hpp"
#include "update.hpp"

namespace pz::ui
{

namespace
{

    constexpr int ABOUT_TAB = 4; // sidebar position: Browse, Installed, Game files, a separator, About

    brls::View* gameFilesTab()
    {
        auto* frame = new brls::ScrollingFrame();
        frame->setGrow(1);
        auto* box = new brls::Box(brls::Axis::COLUMN);
        box->setAlignItems(brls::AlignItems::STRETCH);
        box->setPadding(32, 48, 32, 48);
        box->addView(header("Add game files"));
        box->addView(textBlock("PortZone finds which files each port needs and puts them in the right folder. "
                               "You never have to sort them.",
            20, mutedColor()));

        auto* sendRow = new brls::Box(brls::Axis::ROW);
        sendRow->setMarginTop(16);
        sendRow->addView(button("Send from phone or PC",
            [] { brls::Application::pushActivity(new TransferActivity("")); }, true));
        box->addView(sendRow);
        auto* sendHint = textBlock("Drop the game's files or folders in a browser on the same Wi-Fi.", 18, mutedColor());
        sendHint->setMarginTop(8);
        box->addView(sendHint);

        auto* findRow = new brls::Box(brls::Axis::ROW);
        findRow->setMarginTop(16);
        findRow->addView(button("Find on SD card", [] { brls::Application::pushActivity(new SdSearchActivity("")); }));
        box->addView(findRow);
        auto* findHint = textBlock("Already copied files somewhere on the SD card? PortZone moves them into place.", 18,
            mutedColor());
        findHint->setMarginTop(8);
        box->addView(findHint);
        frame->setContentView(box);
        return frame;
    }

    brls::View* aboutTab(const std::function<void()>& chooseTheme)
    {
        auto* frame = new brls::ScrollingFrame();
        frame->setGrow(1);
        auto* box = new brls::Box(brls::Axis::COLUMN);
        box->setAlignItems(brls::AlignItems::STRETCH);
        box->setPadding(32, 48, 32, 48);
        // Not focusable, so moving into the tab lands on the Theme button.
        box->addView(textBlock("PortZone " PORTZONE_VERSION, 28));
        box->addView(textBlock("A manager for GitHub game projects that use your own game files. PortZone never downloads game data.", 20, mutedColor()));

        box->addView(header("Appearance"));
        auto* themeRow = new brls::Box(brls::Axis::ROW);
        themeRow->setMarginTop(8);
        themeRow->addView(button(std::string("Theme: ") + (settings().lightTheme ? "Light" : "Dark"), chooseTheme));
        box->addView(themeRow);

        // PortZone can't tell a Switch 2 from a Switch: the player says which, for ports with a Switch 2 build.
        box->addView(header("Console"));
        const auto consoleText = [] { return std::string("This console: ") + (config().console == "switch2" ? "Switch 2" : "Switch"); };
        auto consoleButton = std::make_shared<brls::Button*>(nullptr);
        *consoleButton     = button(consoleText(), [consoleButton, consoleText] {
            config().console   = config().console == "switch2" ? "switch" : "switch2";
            settings().console = config().console;
            saveSettings();
            Catalog::get().reapply();
            BrowseView::portUpdatesFound(); // every list redraws for the console's builds
            (*consoleButton)->setText(consoleText());
        });
        auto* consoleRow = new brls::Box(brls::Axis::ROW);
        consoleRow->setMarginTop(8);
        consoleRow->addView(*consoleButton);
        box->addView(consoleRow);
        auto* consoleHint = textBlock("Games with a separate Switch 2 build install that build on a Switch 2; games for the "
                                      "other console are listed but not installed.",
            16, mutedColor());
        consoleHint->setMarginTop(8);
        box->addView(consoleHint);

        box->addView(header("Updates"));
        const update::Status& status = update::current();
        std::string state            = "Not checked yet";
        if (status.available)
            state = status.tag + " is available";
        else if (!status.error.empty())
            state = "Could not check: " + status.error;
        else if (status.checked)
            state = "Up to date";
        box->addView(textRow("PortZone " PORTZONE_VERSION ": " + state, 18, status.available ? goodColor() : mutedColor()));
        auto* updateRow = new brls::Box(brls::Axis::ROW);
        updateRow->setMarginTop(8);
        updateRow->addView(button(status.available ? "See update" : "Check for updates",
            [] { brls::Application::pushActivity(new UpdateActivity()); }, status.available));
        auto self     = std::make_shared<brls::Button*>(nullptr);
        auto* atStart = button(std::string("Check at start: ") + (settings().checkUpdates ? "On" : "Off"), [self] {
            settings().checkUpdates = !settings().checkUpdates;
            saveSettings();
            (*self)->setText(std::string("Check at start: ") + (settings().checkUpdates ? "On" : "Off"));
        });
        *self = atStart;
        updateRow->addView(atStart);
        box->addView(updateRow);
        auto* atStartHint = textBlock("At start PortZone also checks the games it installed; ones with a new release show "
                                      "in Installed and on their cards.",
            16, mutedColor());
        atStartHint->setMarginTop(8);
        box->addView(atStartHint);

        box->addView(header("Sources", "Press Y on any tab to reload"));
        const bool offline = Catalog::get().source != Catalog::Source::Network;
        std::string list   = "Port list: " + config().indexUrl;
        if (Catalog::get().source == Catalog::Source::Cache)
            list += " (offline: last downloaded copy)";
        else if (Catalog::get().source == Catalog::Source::BuiltIn)
            list += " (offline: the list built into PortZone)";
        box->addView(textRow(list));
        box->addView(textRow("SD card: " + config().sdRoot.string()));
        if (!config().draftsDir.empty())
            box->addView(textRow("Draft portzone.json files: " + config().draftsDir.string()));
        box->addView(header("Ports"));
        for (const auto& port : Catalog::get().ports)
        {
            std::string source = port.manifest ? "portzone.json from " + port.manifestSource :
                !port.manifestError.empty()    ? port.manifestError :
                offline                        ? "portzone.json not checked (offline)" :
                                                 "no portzone.json";
            box->addView(textRow(port.info.title + ": " + source, 18, port.manifestError.empty() ? mutedColor() : badColor()));
        }
        frame->setContentView(box);
        return frame;
    }

} // namespace

HomeActivity::~HomeActivity() { *alive = false; }

brls::View* HomeActivity::createContentView()
{
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setGrow(1);
    content->setAlignItems(brls::AlignItems::STRETCH);
    parking = focusParking();
    content->addView(parking);
    root = new brls::Box(brls::Axis::COLUMN);
    root->setGrow(1);
    root->setAlignItems(brls::AlignItems::STRETCH);
    content->addView(root);

    frame = new brls::AppletFrame(content);
    frame->setTitle("PortZone");
    frame->setIcon(BRLS_ASSET("icon/icon.png"));
    frame->registerAction("Refresh", brls::BUTTON_Y, [this](brls::View*) {
        load();
        return true;
    });
    // On a PC keyboard, borealis maps no key to Y: the Y key does it.
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Y), [this](brls::View*) {
        load();
        return true;
    });
    return frame;
}

void HomeActivity::onContentAvailable() { load(); }

void HomeActivity::onResume()
{
    if (brls::View* content = getContentView())
        content->willAppear(false);
    markInstalledTab(); // back from a game's page: it may have just been updated
}

void HomeActivity::load()
{
    if (loading)
        return;
    loading = true;
    tabs    = nullptr; // freed by the rebuild below
    rebuild(root, parking, [](brls::Box* box) {
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->addView(new brls::ProgressSpinner());
        box->addView(label("Loading ports...", 20, mutedColor()));
    });
    // The port list, and (unless turned off) whether a newer PortZone is out: two requests, one wait.
    struct Startup
    {
        Catalog::Loaded catalog;
        update::Status update;
    };
    background<std::shared_ptr<Startup>>(
        alive,
        [] {
            auto startup     = std::make_shared<Startup>();
            startup->catalog = Catalog::fetch();
            if (settings().checkUpdates)
                startup->update = update::check();
            return startup;
        },
        [this](std::shared_ptr<Startup> startup) {
            loading = false;
            if (!startup->catalog.error.empty())
                return showError(startup->catalog.error);
            Catalog::get().apply(std::move(startup->catalog));
            if (startup->update.checked)
            {
                const bool dismissed = update::current().dismissed && update::current().tag == startup->update.tag;
                update::current()    = startup->update;
                update::current().dismissed = dismissed;
            }
            showCatalog(0);
            if (settings().checkUpdates)
                checkPortUpdates();
        });
}

void HomeActivity::showError(const std::string& error)
{
    tabs = nullptr;
    rebuild(root, parking, [this, error](brls::Box* box) {
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        auto* text = label(error, 20, badColor());
        text->setMarginBottom(24);
        box->addView(text);
        auto* retry = button("Try again", [this] { load(); }, true);
        box->addView(retry);
        brls::Application::giveFocus(retry);
    });
}

void HomeActivity::showCatalog(int tab)
{
    rebuild(root, parking, [this, tab](brls::Box* box) {
        box->setJustifyContent(brls::JustifyContent::FLEX_START);
        box->setAlignItems(brls::AlignItems::STRETCH);
        tabs = new brls::TabFrame();
        tabs->setGrow(1);
        tabs->addTab("Browse", [] { return new BrowseView(false); });
        tabs->addTab("Installed", [] { return new BrowseView(true); });
        tabs->addTab("Game files", [] { return gameFilesTab(); });
        tabs->addSeparator();
        tabs->addTab("About", [this] { return aboutTab([this] { chooseTheme(); }); });
        box->addView(tabs);
        tabs->focusTab(tab);
    });
    markInstalledTab();
}

void HomeActivity::checkPortUpdates()
{
    background<std::shared_ptr<std::map<std::string, std::string>>>(
        alive,
        [ports = Catalog::get().ports, testBuilds = settings().testBuilds] {
            return std::make_shared<std::map<std::string, std::string>>(portupdates::check(ports, testBuilds));
        },
        [this](std::shared_ptr<std::map<std::string, std::string>> found) {
            portupdates::found() = std::move(*found);
            markInstalledTab();
            BrowseView::portUpdatesFound();
        });
}

void HomeActivity::markInstalledTab()
{
    if (!tabs)
        return;
    auto* sidebar = dynamic_cast<brls::Sidebar*>(tabs->getView("brls/tab_frame/sidebar"));
    if (!sidebar)
        return;
    const size_t updates = portupdates::count();
    if (brls::SidebarItem* installed = sidebar->getItem(1))
        installed->setLabel(updates ? "Installed (" + std::to_string(updates) + ")" : "Installed");
}

void HomeActivity::chooseTheme()
{
    // Applied once the dropdown has closed, so the whole screen can be rebuilt in the new colours.
    auto* dropdown = new brls::Dropdown(
        "Theme", { "Dark", "Light" }, [](int) {}, settings().lightTheme ? 1 : 0,
        [this](int selected) {
            if (selected < 0 || (selected == 1) == settings().lightTheme)
                return;
            settings().lightTheme = selected == 1;
            saveSettings();
            applyTheme(settings().lightTheme);
            recolorFrame();
            showCatalog(ABOUT_TAB);
        });
    brls::Application::pushActivity(new brls::Activity(dropdown));
}

void HomeActivity::recolorFrame()
{
    // Built once with the old palette: header title, clock and the lines around the content.
    const NVGcolor line = brls::Application::getTheme()["brls/applet_frame/separator"];
    for (const char* id : { "brls/applet_frame/title_label", "brls/hints/time" })
        if (auto* text = dynamic_cast<brls::Label*>(frame->getView(id)))
            text->setTextColor(textColor());
    for (const char* id : { "brls/applet_frame/header", "brls/applet_frame/footer" })
        if (brls::View* view = frame->getView(id))
            view->setLineColor(line);
}

} // namespace pz::ui
