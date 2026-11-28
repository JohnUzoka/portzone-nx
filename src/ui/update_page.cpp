#include "ui/update_page.hpp"

#include "config.hpp"
#include "markdown.hpp"
#include "update.hpp"

#ifndef PORTZONE_VERSION
#define PORTZONE_VERSION "dev"
#endif

using namespace brls::literals;

namespace pz::ui
{

UpdateActivity::~UpdateActivity() { *alive = false; }

brls::View* UpdateActivity::createContentView()
{
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    content->setPadding(24, 48, 48, 48);

    intro = card(brls::Axis::ROW);
    intro->setFocusable(true);
    intro->setHideHighlightBackground(true);
    intro->setAlignItems(brls::AlignItems::CENTER);
    auto* logo = new brls::Image();
    logo->setImageFromRes("icon/icon.png");
    logo->setWidth(72);
    logo->setHeight(72);
    logo->setMarginRight(20);
    intro->addView(logo);
    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setGrow(1);
    text->setShrink(1);
    text->addView(textBlock("PortZone " PORTZONE_VERSION, 28));
    text->addView(textBlock(brls::getStr("portzone/update/source", config().updateRepo), 16, mutedColor()));
    intro->addView(text);
    content->addView(intro);

    statusBox = new brls::Box(brls::Axis::COLUMN);
    statusBox->setAlignItems(brls::AlignItems::STRETCH);
    statusBox->setMarginTop(18);
    content->addView(statusBox);

    notesBox = new brls::Box(brls::Axis::COLUMN);
    notesBox->setAlignItems(brls::AlignItems::STRETCH);
    content->addView(notesBox);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setContentView(content);
    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("portzone/update/title"_i18n);
    return frame;
}

void UpdateActivity::onContentAvailable()
{
    if (auto running = jobFor("@portzone"); running && !running->finished)
    {
        job = running;
        poll(job);
    }
    if (update::current().checked)
        refresh();
    else
        check();
}

void UpdateActivity::check()
{
    checking = true;
    refresh();
    background<std::shared_ptr<update::Status>>(alive, [] { return std::make_shared<update::Status>(update::check()); },
        [this](std::shared_ptr<update::Status> status) {
            checking          = false;
            update::current() = *status;
            refresh();
        });
}

void UpdateActivity::refresh()
{
    rebuild(statusBox, intro, [this](brls::Box* box) { fillStatus(box); });
    rebuild(notesBox, intro, [this](brls::Box* box) { fillNotes(box); });
}

void UpdateActivity::fillStatus(brls::Box* box)
{
    const update::Status& status = update::current();
    const bool busy              = job && !job->finished;
    const bool updated           = job && job->finished && job->error().empty();

    auto* actions = new brls::Box(brls::Axis::ROW);
    actions->setAlignItems(brls::AlignItems::CENTER);
    actions->setMarginTop(14);

    if (checking)
        box->addView(pill("portzone/update/checking"_i18n, mutedColor()));
    else if (updated)
    {
        box->addView(pill(brls::getStr("portzone/update/updated_to", status.tag), goodColor()));
        box->addView(textBlock("portzone/update/starts_next"_i18n, 18, mutedColor()));
        actions->addView(button("portzone/update/restart"_i18n, [] { update::restart(); }, true));
    }
    else if (busy)
    {
        box->addView(pill(brls::getStr("portzone/update/updating_to", status.tag), accentColor()));
        actions->addView(button("portzone/common/cancel"_i18n, [this] {
            if (job)
                job->cancel = true;
        }));
    }
    else if (!status.error.empty())
    {
        box->addView(pill("portzone/update/could_not_check"_i18n, badColor()));
        box->addView(textBlock(status.error, 18, mutedColor()));
    }
    else if (status.available)
    {
        box->addView(pill(brls::getStr("portzone/common/is_available", status.tag), goodColor()));
        if (update::canInstall())
            actions->addView(button(brls::getStr("portzone/common/update_to_size", status.tag, formatSize(status.asset.size)),
                [this] { startUpdate(); }, true));
        else
            box->addView(textBlock(brls::getStr("portzone/update/cannot_replace", status.asset.name), 18, mutedColor()));
    }
    else if (status.checked)
        box->addView(pill("portzone/update/up_to_date"_i18n, goodColor()));

    if (!checking && !busy && !updated)
        actions->addView(button("portzone/update/check_again"_i18n, [this] { check(); }));
    if (!actions->getChildren().empty())
        box->addView(actions);
    else
        delete actions;

    progress = label("", 18, accentColor());
    progress->setMarginTop(8);
    if (job && !updated)
    {
        const std::string error = job->error();
        if (!error.empty())
            progress->setTextColor(badColor());
        progress->setText(!error.empty() ? error : job->stage());
    }
    box->addView(progress);
}

void UpdateActivity::fillNotes(brls::Box* box)
{
    const update::Status& status = update::current();
    if (status.tag.empty() || status.notes.empty())
        return;
    box->addView(header(status.available ? brls::getStr("portzone/update/whats_new", status.tag) :
                                           brls::getStr("portzone/update/about_tag", status.tag)));
    addReleaseNotes(box, status.notes);
}

void addReleaseNotes(brls::Box* box, const std::string& markdown)
{
    for (const auto& block : parseMarkdown(markdown))
    {
        if (block.kind == TextBlock::Kind::Heading)
            box->addView(textRow(block.text, 21, accentColor()));
        else if (block.kind == TextBlock::Kind::Bullet)
            box->addView(textRow("•  " + block.text, 18, textColor(), 28.0f * float(block.level + 1)));
        else
            box->addView(textRow(block.text, 18, block.kind == TextBlock::Kind::Code ? mutedColor() : textColor()));
    }
}

WhatsNewActivity::WhatsNewActivity(bool firstStart)
    : firstStart(firstStart)
{
}

WhatsNewActivity::~WhatsNewActivity() { *alive = false; }

brls::View* WhatsNewActivity::createContentView()
{
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    content->setPadding(24, 48, 48, 48);

    auto* intro = card(brls::Axis::ROW);
    intro->setAlignItems(brls::AlignItems::CENTER);
    auto* logo = new brls::Image();
    logo->setImageFromRes("icon/icon.png");
    logo->setWidth(72);
    logo->setHeight(72);
    logo->setMarginRight(20);
    intro->addView(logo);
    intro->addView(textBlock(firstStart ? "portzone/whatsnew/welcome_title"_i18n :
                                          brls::getStr("portzone/whatsnew/title", PORTZONE_VERSION),
        28));
    content->addView(intro);

    // Continue first, so A closes the page at once; the notes follow below it.
    auto* actions = new brls::Box(brls::Axis::ROW);
    actions->setMarginTop(18);
    actions->addView(button("portzone/whatsnew/continue"_i18n, [] { brls::Application::popActivity(); }, true));
    content->addView(actions);

    notesBox = new brls::Box(brls::Axis::COLUMN);
    notesBox->setAlignItems(brls::AlignItems::STRETCH);
    notesBox->setMarginTop(12);
    if (firstStart)
        for (const char* key : { "portzone/whatsnew/welcome_1", "portzone/whatsnew/welcome_2", "portzone/whatsnew/welcome_3",
                 "portzone/whatsnew/welcome_4" })
        {
            auto* paragraph = textBlock(brls::getStr(key), 19);
            paragraph->setMarginTop(10);
            notesBox->addView(paragraph);
        }
    else
        notesBox->addView(textRow("portzone/whatsnew/loading"_i18n, 18, mutedColor()));
    content->addView(notesBox);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setContentView(content);
    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle(firstStart ? "portzone/whatsnew/welcome_title"_i18n : "portzone/update/title"_i18n);
    return frame;
}

void WhatsNewActivity::onContentAvailable()
{
    if (firstStart)
        return;
    // This version's release notes, from the same releases PortZone updates from.
    const std::string tag = std::string("v") + PORTZONE_VERSION;
    background<std::shared_ptr<std::string>>(alive,
        [tag] {
            for (const auto& release : github::fetchReleases("https://github.com/" + config().updateRepo).releases)
                if (release.tag == tag)
                    return std::make_shared<std::string>(release.notes);
            return std::make_shared<std::string>();
        },
        [this](std::shared_ptr<std::string> notes) {
            notesBox->clearViews(); // focus is on Continue, outside this box
            if (notes->empty())
                notesBox->addView(textRow(brls::getStr("portzone/whatsnew/no_notes", PORTZONE_VERSION, config().updateRepo),
                    18, mutedColor()));
            else
                addReleaseNotes(notesBox, *notes);
        });
}

void UpdateActivity::startUpdate()
{
    const update::Status& status = update::current();
    auto started                 = startSelfUpdate(status.asset, status.tag);
    if (!started)
        return;
    job = started;
    refresh();
    poll(job);
}

void UpdateActivity::poll(std::shared_ptr<Job> watched)
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
            progress->setText(brls::getStr("portzone/common/progress", watched->stage(), int(watched->fraction * 100)));
        poll(watched);
    });
}

} // namespace pz::ui
