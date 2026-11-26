#include "ui/update_page.hpp"

#include "config.hpp"
#include "markdown.hpp"
#include "update.hpp"

#ifndef PORTZONE_VERSION
#define PORTZONE_VERSION "dev"
#endif

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
    text->addView(textBlock("Updates come from github.com/" + config().updateRepo + "/releases.", 16, mutedColor()));
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
    frame->setTitle("PortZone updates");
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
        box->addView(pill("Checking GitHub...", mutedColor()));
    else if (updated)
    {
        box->addView(pill("Updated to " + status.tag, goodColor()));
        box->addView(textBlock("The new PortZone starts the next time you open it.", 18, mutedColor()));
        actions->addView(button("Restart now", [] { update::restart(); }, true));
    }
    else if (busy)
    {
        box->addView(pill("Updating to " + status.tag, accentColor()));
        actions->addView(button("Cancel", [this] {
            if (job)
                job->cancel = true;
        }));
    }
    else if (!status.error.empty())
    {
        box->addView(pill("Could not check for updates", badColor()));
        box->addView(textBlock(status.error, 18, mutedColor()));
    }
    else if (status.available)
    {
        box->addView(pill(status.tag + " is available", goodColor()));
        if (update::canInstall())
            actions->addView(button("Update to " + status.tag + "  (" + formatSize(status.asset.size) + ")",
                [this] { startUpdate(); }, true));
        else
            box->addView(textBlock("This copy cannot replace itself (it is not the Switch build started from the SD card). "
                                   "Get " + status.asset.name + " from the releases page.",
                18, mutedColor()));
    }
    else if (status.checked)
        box->addView(pill("PortZone is up to date", goodColor()));

    if (!checking && !busy && !updated)
        actions->addView(button("Check again", [this] { check(); }));
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
    box->addView(header(status.available ? "What's new in " + status.tag : "About " + status.tag));
    for (const auto& block : parseMarkdown(status.notes))
    {
        if (block.kind == TextBlock::Kind::Heading)
            box->addView(textRow(block.text, 21, accentColor()));
        else if (block.kind == TextBlock::Kind::Bullet)
            box->addView(textRow("•  " + block.text, 18, textColor(), 28.0f * float(block.level + 1)));
        else
            box->addView(textRow(block.text, 18, block.kind == TextBlock::Kind::Code ? mutedColor() : textColor()));
    }
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
            progress->setText(watched->stage() + "  " + std::to_string(int(watched->fraction * 100)) + "%");
        poll(watched);
    });
}

} // namespace pz::ui
