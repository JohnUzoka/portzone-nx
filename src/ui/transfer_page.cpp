#include "ui/transfer_page.hpp"

#include <algorithm>

#include "catalog.hpp"
#include "config.hpp"
#include "transfer.hpp"

using namespace brls::literals;

namespace pz::ui
{

TransferActivity::TransferActivity(std::string focusPort)
    : focusPort(std::move(focusPort))
{
}

TransferActivity::~TransferActivity()
{
    *alive = false;
    transfer::stop();
    brls::Application::getPlatform()->disableScreenDimming(false);
}

brls::View* TransferActivity::createContentView()
{
    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    content->setPadding(24, 48, 48, 48);

    intro = card(brls::Axis::COLUMN);
    intro->setFocusable(true);
    intro->setHideHighlightBackground(true);
    intro->addView(textBlock("portzone/transfer/step_wifi"_i18n, 20));
    intro->addView(textBlock("portzone/transfer/step_open"_i18n, 20));
    auto* addressRow = new brls::Box(brls::Axis::ROW);
    addressRow->setMarginTop(10);
    addressRow->setMarginBottom(10);
    address = label("portzone/transfer/starting"_i18n, 34, accentColor());
    addressRow->addView(address);
    intro->addView(addressRow);
    intro->addView(textBlock("portzone/transfer/drop_hint"_i18n, 18, mutedColor()));
    content->addView(intro);

    statusBox = new brls::Box(brls::Axis::COLUMN);
    statusBox->setAlignItems(brls::AlignItems::STRETCH);
    statusBox->setMarginTop(18);
    content->addView(statusBox);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setContentView(content);
    auto* frame       = new brls::AppletFrame(scroll);
    std::string title = "portzone/transfer/title"_i18n;
    if (!focusPort.empty())
        if (const Port* port = Catalog::get().find(focusPort))
            title = port->info.title + " · " + title;
    frame->setTitle(title);
    return frame;
}

void TransferActivity::onContentAvailable()
{
    brls::Application::getPlatform()->disableScreenDimming(true);
    start();
    poll();
}

void TransferActivity::start()
{
    transfer::start(Catalog::get().ports, focusPort);
    refresh();
}

void TransferActivity::refresh()
{
    const transfer::Status status = transfer::status();
    shown                         = status.generation;
    address->setText(status.running ? status.address :
                                      (status.error.empty() ? "portzone/transfer/starting"_i18n : "portzone/transfer/not_available"_i18n));
    rebuild(statusBox, intro, [this, &status](brls::Box* box) {
        if (!status.error.empty())
        {
            box->addView(textBlock(status.error, 20, badColor()));
            auto* actions = new brls::Box(brls::Axis::ROW);
            actions->setMarginTop(14);
            actions->addView(button("portzone/common/try_again"_i18n, [this] { start(); }, true));
            box->addView(actions);
            return;
        }
        fillStatus(box);
    });
}

void TransferActivity::fillStatus(brls::Box* box)
{
    const transfer::Status status = transfer::status();
    if (!status.phase.empty())
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->addView(pill(status.phase, status.currentFile.empty() ? mutedColor() : accentColor()));
        box->addView(row);
    }
    if (!status.currentFile.empty())
    {
        std::string line = status.currentFile + " · " +
            brls::getStr("portzone/transfer/size_of", formatSize(status.currentDone), formatSize(status.currentTotal));
        if (status.bytesPerSecond > 0)
            line += " · " + formatSize(uint64_t(status.bytesPerSecond)) + "/s";
        box->addView(textRow(line, 20));
    }
    if (status.filesTotal > 0)
        box->addView(textRow(brls::getStr("portzone/transfer/files_of", status.filesDone, status.filesTotal),
            18, mutedColor()));
    const size_t count = std::min<size_t>(status.log.size(), 12);
    for (size_t i = 0; i < count; i++)
        box->addView(textRow(status.log[status.log.size() - 1 - i], 17, mutedColor()));
}

void TransferActivity::poll()
{
    brls::delay(300, [this, alive = alive] {
        if (!*alive)
            return;
        if (transfer::status().generation != shown)
            refresh();
        poll();
    });
}

} // namespace pz::ui
