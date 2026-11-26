#include "ui/sdsearch_page.hpp"

#include "config.hpp"
#include "gamefiles.hpp"
#include "ui/transfer_page.hpp"

namespace pz::ui
{

namespace
{
    std::string folderOf(const std::string& path)
    {
        const size_t slash = path.rfind('/');
        return slash == std::string::npos ? "" : path.substr(0, slash);
    }

    // The deepest folder holding every one of `paths`.
    std::string commonFolder(const std::vector<std::string>& paths)
    {
        std::string common = folderOf(paths.front());
        for (const auto& path : paths)
            while (!common.empty() && path.rfind(common + "/", 0) != 0)
                common = folderOf(common);
        return common;
    }

    struct SearchResult
    {
        std::vector<matcher::Source> sources;
        matcher::Plan plan;
    };
}

SdSearchActivity::SdSearchActivity(std::string focusPort)
    : focusPort(std::move(focusPort))
{
}

SdSearchActivity::~SdSearchActivity()
{
    *alive = false;
    if (cancel)
        *cancel = true;
}

brls::View* SdSearchActivity::createContentView()
{
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    content->setPadding(24, 48, 48, 48);

    intro = card(brls::Axis::COLUMN);
    intro->setFocusable(true);
    intro->setHideHighlightBackground(true);
    intro->addView(textBlock("PortZone looks through the SD card for files the ports need.", 20));
    intro->addView(textBlock("Files you choose are moved (not copied) into each port's folder.", 18, mutedColor()));
    content->addView(intro);

    resultsBox = new brls::Box(brls::Axis::COLUMN);
    resultsBox->setAlignItems(brls::AlignItems::STRETCH);
    resultsBox->setMarginTop(18);
    content->addView(resultsBox);

    actionsBox = new brls::Box(brls::Axis::COLUMN);
    actionsBox->setAlignItems(brls::AlignItems::STRETCH);
    content->addView(actionsBox);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setContentView(content);
    auto* frame       = new brls::AppletFrame(scroll);
    std::string title = "Find on SD card";
    if (!focusPort.empty())
        if (const Port* port = Catalog::get().find(focusPort))
            title = port->info.title + " · " + title;
    frame->setTitle(title);
    return frame;
}

void SdSearchActivity::onContentAvailable() { search(); }

void SdSearchActivity::search()
{
    auto ports = std::make_shared<std::vector<Port>>();
    for (const auto& port : Catalog::get().ports)
        if (port.manifest && port.manifest->gameData && (focusPort.empty() || port.info.id == focusPort))
            ports->push_back(port);

    if (cancel)
        *cancel = true;
    cancel    = std::make_shared<std::atomic<bool>>(false);
    seen      = std::make_shared<std::atomic<size_t>>(0);
    searching = true;
    job       = nullptr;
    sources.clear();
    plan = {};
    chosen.clear();
    refresh();
    pollSearch();

    auto watchedCancel = cancel;
    background<std::shared_ptr<SearchResult>>(alive,
        [ports, cancel = cancel, seen = seen] {
            std::vector<const Port*> ptrs;
            for (const auto& port : *ports)
                ptrs.push_back(&port);
            auto result     = std::make_shared<SearchResult>();
            result->sources = gamefiles::listSdCard(ptrs, *cancel, *seen);
            if (!*cancel)
                result->plan = matcher::plan(result->sources, ptrs);
            return result;
        },
        [this, watchedCancel](std::shared_ptr<SearchResult> result) {
            if (cancel != watchedCancel)
                return; // a newer search replaced this one
            searching = false;
            sources   = std::move(result->sources);
            plan      = std::move(result->plan);
            chosen.clear();
            for (const auto& match : plan.matches)
                chosen.push_back(match.selected);
            refresh();
        });
}

void SdSearchActivity::pollSearch()
{
    brls::delay(300, [this, alive = alive, watched = cancel] {
        if (!*alive || !searching || cancel != watched)
            return;
        if (progress)
            progress->setText("Searching the SD card… " + std::to_string(seen->load()) + " files looked at");
        pollSearch();
    });
}

void SdSearchActivity::refresh()
{
    progress = nullptr;
    marks.clear();
    rebuild(resultsBox, intro, [this](brls::Box* box) { fillResults(box); });
    rebuild(actionsBox, intro, [this](brls::Box* box) { fillActions(box); });
}

void SdSearchActivity::fillResults(brls::Box* box)
{
    if (searching)
    {
        progress = label("Searching the SD card… " + std::to_string(seen->load()) + " files looked at", 20,
            accentColor());
        box->addView(progress);
        auto* actions = new brls::Box(brls::Axis::ROW);
        actions->setMarginTop(14);
        actions->addView(button("Cancel", [this] {
            *cancel   = true;
            searching = false;
            refresh();
        }));
        box->addView(actions);
        return;
    }

    if (job)
    {
        progress = label("", 20, accentColor());
        if (job->finished)
        {
            const std::string error = job->error();
            progress->setTextColor(error.empty() ? goodColor() : badColor());
            progress->setText(error.empty() ? job->stage() : error);
        }
        else
            progress->setText(job->stage() + "  " + std::to_string(int(job->fraction * 100)) + "%");
        box->addView(progress);
        return;
    }

    if (plan.matches.empty())
    {
        box->addView(textBlock(cancel && *cancel ? "Search cancelled." : "No game files found outside the ports' folders.",
            20));
        box->addView(textBlock("System folders, PortZone's data and the ports' own folders are not searched.", 18,
            mutedColor()));
        return;
    }

    box->addView(textBlock("Choose what to move. Moved files leave their old folder.", 18, mutedColor()));
    for (size_t i = 0; i < plan.matches.size(); i++)
    {
        const matcher::Match& match = plan.matches[i];

        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(10, 8, 10, 8);
        row->setAlignItems(brls::AlignItems::CENTER);
        auto* mark = label(chosen[i] ? "✓" : "○", 26, chosen[i] ? goodColor() : mutedColor());
        mark->setWidth(44);
        row->addView(mark);
        marks.push_back(mark);

        auto* text = new brls::Box(brls::Axis::COLUMN);
        text->setGrow(1);
        text->setShrink(1);
        text->addView(textBlock(match.portTitle + " · " + match.inputLabel, 20));
        const std::string gameDir = sdShown(match.gameDir);
        std::string detail;
        if (match.files.size() == 1)
        {
            const matcher::Placement& file = match.files.front();
            detail = sources[file.source].path + " → " + gameDir + "/" + file.dest + " · " + formatSize(match.bytes);
        }
        else
        {
            std::vector<std::string> from, to;
            for (const auto& file : match.files)
            {
                from.push_back(sources[file.source].path);
                to.push_back(file.dest);
            }
            const std::string fromDir = commonFolder(from);
            const std::string toDir   = commonFolder(to);
            detail = (fromDir.empty() ? "" : fromDir + "/") + " (" + std::to_string(match.files.size()) + " files, " +
                formatSize(match.bytes) + ") → " + gameDir + (toDir.empty() ? "" : "/" + toDir) + "/";
        }
        text->addView(textBlock(detail, 16, mutedColor()));
        if (match.alreadyMet)
            text->addView(textBlock("Already in place; moving replaces it", 16, accentColor()));
        row->addView(text);

        row->registerClickAction([this, i](brls::View*) {
            toggle(i);
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));
        box->addView(row);
    }
}

void SdSearchActivity::fillActions(brls::Box* box)
{
    auto* actions = new brls::Box(brls::Axis::ROW);
    actions->setMarginTop(14);
    actions->setAlignItems(brls::AlignItems::CENTER);

    if (job && job->finished)
        actions->addView(button("Search again", [this] { search(); }, true));
    else if (!searching && !job)
    {
        size_t count   = 0;
        uint64_t bytes = 0;
        for (size_t i = 0; i < plan.matches.size(); i++)
            if (chosen[i])
            {
                count++;
                bytes += plan.matches[i].bytes;
            }
        if (count > 0)
            actions->addView(button("Move " + std::to_string(count) + " selected (" + formatSize(bytes) + ")",
                [this] { move(); }, true));
        if (plan.matches.empty())
            actions->addView(button("Send from phone or PC instead",
                [this] { brls::Application::pushActivity(new TransferActivity(focusPort)); }, true));
        actions->addView(button("Search again", [this] { search(); }));
    }

    if (!actions->getChildren().empty())
        box->addView(actions);
    else
        delete actions;
}

void SdSearchActivity::toggle(size_t index)
{
    const bool on = !chosen[index];
    if (on && plan.matches[index].group >= 0)
        for (size_t i = 0; i < plan.matches.size(); i++)
            if (plan.matches[i].group == plan.matches[index].group)
                chosen[i] = false;
    chosen[index] = on;
    for (size_t i = 0; i < marks.size(); i++)
    {
        marks[i]->setText(chosen[i] ? "✓" : "○");
        marks[i]->setTextColor(chosen[i] ? goodColor() : mutedColor());
    }
    rebuild(actionsBox, intro, [this](brls::Box* box) { fillActions(box); });
}

void SdSearchActivity::move()
{
    std::vector<matcher::Match> picked;
    for (size_t i = 0; i < plan.matches.size(); i++)
        if (chosen[i])
            picked.push_back(plan.matches[i]);
    auto started = gamefiles::startMove(sources, std::move(picked));
    if (!started)
        return;
    job = started;
    refresh();
    pollMove(job);
}

void SdSearchActivity::pollMove(std::shared_ptr<Job> watched)
{
    brls::delay(200, [this, alive = alive, watched] {
        if (!*alive || job != watched)
            return;
        if (watched->finished)
        {
            refresh();
            return;
        }
        if (progress)
            progress->setText(watched->stage() + "  " + std::to_string(int(watched->fraction * 100)) + "%");
        pollMove(watched);
    });
}

} // namespace pz::ui
