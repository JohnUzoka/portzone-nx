#include "ui/home.hpp"

#include "config.hpp"
#include "languages.hpp"
#include "port_updates.hpp"
#include "settings.hpp"
#include "ui/browse.hpp"
#include "ui/sdsearch_page.hpp"
#include "ui/transfer_page.hpp"
#include "ui/update_page.hpp"
#include "update.hpp"

using namespace brls::literals;

namespace pz::ui
{

namespace
{

    constexpr int ABOUT_TAB = 4; // sidebar position: Browse, Installed, Game files, a separator, About

    // A tab's content, built once the sidebar's focus has rested on it (borealis builds a tab whenever its sidebar
    // item gains focus, so moving down from Browse to About would build Installed and Game files on the way). With a
    // cache, the content is kept when another tab is shown, and coming back shows it again at once.
    class LazyTab : public brls::Box
    {
      public:
        LazyTab(std::shared_ptr<TabCache> cache, int slot, std::function<brls::View*()> make)
            : brls::Box(brls::Axis::COLUMN)
            , cache(std::move(cache))
        {
            setGrow(1);
            setAlignItems(brls::AlignItems::STRETCH);
            if (this->cache && this->cache->views.count(slot))
            {
                content = this->cache->views[slot];
                addView(content); // its lists read their ports' states again
                return;
            }
            brls::delay(120, [this, slot, alive = alive, make = std::move(make)] {
                if (!*alive)
                    return;
                content = make();
                if (this->cache)
                    this->cache->views[slot] = content;
                addView(content);
            });
        }
        ~LazyTab() override
        {
            *alive = false;
            // Kept for next time: taken out before Box's destructor deletes it, and without a parent, which is
            // about to be gone (a detached view still lays itself out through its parent).
            if (cache && content)
            {
                removeView(content, false);
                content->setParent(nullptr);
            }
        }

      private:
        std::shared_ptr<TabCache> cache;
        brls::View* content = nullptr;
        AliveFlag alive     = std::make_shared<bool>(true);
    };

    brls::View* gameFilesTab()
    {
        auto* frame = new brls::ScrollingFrame();
        frame->setGrow(1);
        auto* box = new brls::Box(brls::Axis::COLUMN);
        box->setAlignItems(brls::AlignItems::STRETCH);
        box->setPadding(32, 48, 32, 48);
        box->addView(header("portzone/home/add_game_files"_i18n));
        box->addView(textBlock("portzone/home/game_files_intro"_i18n, 20, mutedColor()));

        auto* sendRow = new brls::Box(brls::Axis::ROW);
        sendRow->setMarginTop(16);
        sendRow->addView(button("portzone/common/send_from_phone"_i18n,
            [] { brls::Application::pushActivity(new TransferActivity("")); }, true));
        box->addView(sendRow);
        auto* sendHint = textBlock("portzone/home/send_hint"_i18n, 18, mutedColor());
        sendHint->setMarginTop(8);
        box->addView(sendHint);

        auto* findRow = new brls::Box(brls::Axis::ROW);
        findRow->setMarginTop(16);
        findRow->addView(button("portzone/common/find_on_sd"_i18n, [] { brls::Application::pushActivity(new SdSearchActivity("")); }));
        box->addView(findRow);
        auto* findHint = textBlock("portzone/home/find_hint"_i18n, 18, mutedColor());
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
        box->addView(textBlock("portzone/home/about_intro"_i18n, 20, mutedColor()));

        box->addView(header("portzone/home/appearance"_i18n));
        auto* themeRow = new brls::Box(brls::Axis::ROW);
        themeRow->setMarginTop(8);
        themeRow->addView(button(brls::getStr("portzone/home/theme_button",
                                     settings().lightTheme ? "portzone/home/light"_i18n : "portzone/home/dark"_i18n),
            chooseTheme));
        box->addView(themeRow);

        // PortZone can't tell a Switch 2 from a Switch: the player says which, for ports with a Switch 2 build.
        box->addView(header("portzone/home/console"_i18n));
        const auto consoleText = [] { return brls::getStr("portzone/home/this_console", config().console == "switch2" ? "Switch 2" : "Switch"); };
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
        auto* consoleHint = textBlock("portzone/home/console_hint"_i18n, 16, mutedColor());
        consoleHint->setMarginTop(8);
        box->addView(consoleHint);

        // borealis loads the text once at start, so a new choice shows from the next start.
        box->addView(header("portzone/home/language_header"_i18n));
        // "Console's language (Deutsch)": the language PortZone would pick from the console itself.
        const auto automatic = [] {
            std::string name = "English";
            for (const Language& language : languages())
                if (appLanguage("") == language.code)
                    name = language.name;
            return brls::getStr("portzone/home/language_auto", name);
        };
        const auto languageText = [automatic] {
            std::string name = automatic();
            for (const Language& language : languages())
                if (settings().language == language.code)
                    name = language.name;
            return brls::getStr("portzone/home/language_button", name);
        };
        auto languageButton = std::make_shared<brls::Button*>(nullptr);
        *languageButton     = button(languageText(), [languageButton, languageText, automatic] {
            // The console's language first, then each language by its own name.
            std::vector<std::string> names { automatic() };
            int selected = 0;
            for (size_t i = 0; i < languages().size(); i++)
            {
                names.push_back(languages()[i].name);
                if (settings().language == languages()[i].code)
                    selected = int(i) + 1;
            }
            auto* dropdown = new brls::Dropdown("portzone/home/language_header"_i18n, names, [](int) {}, selected,
                [languageButton, languageText](int chosen) {
                    if (chosen < 0 || chosen > int(languages().size()))
                        return;
                    settings().language = chosen == 0 ? "" : languages()[size_t(chosen) - 1].code;
                    saveSettings();
                    (*languageButton)->setText(languageText());
                });
            brls::Application::pushActivity(new brls::Activity(dropdown));
        });
        auto* languageRow = new brls::Box(brls::Axis::ROW);
        languageRow->setMarginTop(8);
        languageRow->addView(*languageButton);
        box->addView(languageRow);
        auto* languageHint = textBlock("portzone/home/language_hint"_i18n, 16, mutedColor());
        languageHint->setMarginTop(8);
        box->addView(languageHint);

        box->addView(header("portzone/home/updates"_i18n));
        const update::Status& status = update::current();
        std::string state            = "portzone/home/not_checked"_i18n;
        if (status.available)
            state = brls::getStr("portzone/common/is_available", status.tag);
        else if (!status.error.empty())
            state = brls::getStr("portzone/home/could_not_check", status.error);
        else if (status.checked)
            state = "portzone/home/up_to_date"_i18n;
        box->addView(textRow(brls::getStr("portzone/home/version_state", PORTZONE_VERSION, state), 18,
            status.available ? goodColor() : mutedColor()));
        auto* updateRow = new brls::Box(brls::Axis::ROW);
        updateRow->setMarginTop(8);
        updateRow->addView(button(status.available ? "portzone/common/see_update"_i18n : "portzone/home/check_for_updates"_i18n,
            [] { brls::Application::pushActivity(new UpdateActivity()); }, status.available));
        const auto atStartText = [] {
            return brls::getStr("portzone/home/check_at_start", settings().checkUpdates ? "portzone/common/on"_i18n : "portzone/common/off"_i18n);
        };
        auto self     = std::make_shared<brls::Button*>(nullptr);
        auto* atStart = button(atStartText(), [self, atStartText] {
            settings().checkUpdates = !settings().checkUpdates;
            saveSettings();
            (*self)->setText(atStartText());
        });
        *self = atStart;
        updateRow->addView(atStart);
        box->addView(updateRow);
        auto* atStartHint = textBlock("portzone/home/at_start_hint"_i18n, 16, mutedColor());
        atStartHint->setMarginTop(8);
        box->addView(atStartHint);

        box->addView(header("portzone/home/sources"_i18n, "portzone/home/sources_hint"_i18n));
        const char* listKey = "portzone/home/port_list";
        if (Catalog::get().source == Catalog::Source::Cache)
            listKey = "portzone/home/port_list_cache";
        else if (Catalog::get().source == Catalog::Source::BuiltIn)
            listKey = "portzone/home/port_list_builtin";
        box->addView(textRow(brls::getStr(listKey, config().indexUrl)));
        box->addView(textRow(brls::getStr("portzone/home/sd_card", config().sdRoot.string())));
        box->addView(textRow("portzone/home/sgdb_credit"_i18n, 18, mutedColor()));
        if (!config().draftsDir.empty())
            box->addView(textRow(brls::getStr("portzone/home/drafts", config().draftsDir.string())));
        // Only the ports with something to say: a portzone.json, or one that couldn't be used. A row for each of the
        // hundreds of others would make this tab slow to open and long to scroll.
        size_t withManifest = 0;
        for (const auto& port : Catalog::get().ports)
            withManifest += port.manifest ? 1 : 0;
        box->addView(header("portzone/home/ports"_i18n,
            brls::getStr("portzone/home/ports_summary", withManifest, Catalog::get().ports.size())));
        for (const auto& port : Catalog::get().ports)
        {
            if (!port.manifest && port.manifestError.empty())
                continue;
            const std::string source =
                port.manifest ? brls::getStr("portzone/home/manifest_from", port.manifestSource) : port.manifestError;
            box->addView(textRow(brls::getStr("portzone/home/port_line", port.info.title, source), 18,
                port.manifestError.empty() ? mutedColor() : badColor()));
        }
        frame->setContentView(box);
        return frame;
    }

} // namespace

TabCache::~TabCache()
{
    for (auto& [tab, view] : views)
        delete view;
}

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
    frame->registerAction("portzone/home/refresh"_i18n, brls::BUTTON_Y, [this](brls::View*) {
        load();
        return true;
    });
#ifndef __SWITCH__
    // On a PC keyboard, borealis maps no key to Y: the Y key does it (not on a Switch, see BrowseView).
    frame->registerAction(brls::BrlsKeyCombination(brls::BRLS_KBD_KEY_Y), [this](brls::View*) {
        load();
        return true;
    });
#endif
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
        box->addView(label("portzone/home/loading"_i18n, 20, mutedColor()));
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
            // Once per version: the release notes after an update, a welcome on the very first start.
            if (settings().lastVersion != PORTZONE_VERSION)
            {
                const bool firstStart  = settings().lastVersion.empty();
                settings().lastVersion = PORTZONE_VERSION;
                saveSettings();
                brls::Application::pushActivity(new WhatsNewActivity(firstStart));
            }
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
        auto* retry = button("portzone/common/try_again"_i18n, [this] { load(); }, true);
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
        tabCache = std::make_shared<TabCache>(); // the old one goes with the old tabs, deleting what it kept
        const auto lazy = [this](int slot, std::function<brls::View*()> make) {
            return [cache = tabCache, slot, make = std::move(make)] { return new LazyTab(cache, slot, make); };
        };
        tabs->addTab("portzone/home/tab_browse"_i18n, lazy(0, [] { return new BrowseView(false); }));
        tabs->addTab("portzone/home/tab_installed"_i18n, lazy(1, [] { return new BrowseView(true); }));
        tabs->addTab("portzone/home/tab_game_files"_i18n, lazy(2, [] { return gameFilesTab(); }));
        tabs->addSeparator();
        // Not kept: it shows the update check's result, which arrives after start.
        tabs->addTab("portzone/home/tab_about"_i18n,
            [this] { return new LazyTab(nullptr, 0, [this] { return aboutTab([this] { chooseTheme(); }); }); });
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
        installed->setLabel(updates ? brls::getStr("portzone/home/tab_installed_count", updates) : "portzone/home/tab_installed"_i18n);
}

void HomeActivity::chooseTheme()
{
    // Applied once the dropdown has closed, so the whole screen can be rebuilt in the new colours.
    auto* dropdown = new brls::Dropdown(
        "portzone/home/theme"_i18n, { "portzone/home/dark"_i18n, "portzone/home/light"_i18n }, [](int) {},
        settings().lightTheme ? 1 : 0,
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
