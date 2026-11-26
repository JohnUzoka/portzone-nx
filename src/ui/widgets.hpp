#pragma once

#include <functional>
#include <memory>
#include <string>

#include <borealis.hpp>

#include "threads.hpp"

namespace pz::ui
{

// Writes PortZone's dark and light palettes into borealis' themes and picks one.
void applyTheme(bool light);

// Colours of the current palette.
NVGcolor textColor();
NVGcolor mutedColor();
NVGcolor goodColor();
NVGcolor badColor();
NVGcolor accentColor();
NVGcolor cardColor();
// Green when all set, red when installed but something is missing, grey otherwise.
NVGcolor stateColor(bool good, bool installed);

brls::Label* label(const std::string& text, float size = 20, NVGcolor color = textColor());

brls::Header* header(const std::string& title, const std::string& subtitle = "");

brls::Button* button(const std::string& text, std::function<void()> onClick, bool primary = false);

// A focusable row of text, so a controller can scroll through long text.
brls::Box* textRow(const std::string& text, float size = 20, NVGcolor color = textColor(), float indent = 0);

// Text for a column: a label inside a row, which measures wrapped text correctly.
brls::Box* textBlock(const std::string& text, float size = 20, NVGcolor color = textColor());

// A small rounded tag: "Ready to play", "Build on PC".
brls::Box* pill(const std::string& text, NVGcolor color);

// A square with the title's initials, tinted by `id`; stands in for a port's icon.
brls::Box* tile(const std::string& title, const std::string& id, float size);

// A rounded panel.
brls::Box* card(brls::Axis axis);

// A box that skips drawing itself and everything in it while it is scrolled out of sight. borealis only skips leaf
// views, so a long list of boxes would otherwise draw every entry each frame.
class CulledBox : public brls::Box
{
  public:
    using brls::Box::Box;
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
        brls::FrameContext* ctx) override;
};

// A 1x1 focusable view with no highlight: where focus waits while a whole screen is rebuilt.
brls::Box* focusParking();

// Empties `box` and refills it. If focus was inside, it moves to `fallbackFocus` (which must live outside `box`)
// first, then to the first focusable view of the new content.
void rebuild(brls::Box* box, brls::View* fallbackFocus, const std::function<void(brls::Box*)>& fill);

// Set to false when the owning view is deleted; callbacks from other threads check it.
using AliveFlag = std::shared_ptr<bool>;

// The initials tile, replaced by the image at `url` once it has downloaded. The download starts the first time the
// tile is drawn, so a long list only fetches the icons that come on screen.
brls::Box* remoteIcon(const std::string& url, const std::string& title, const std::string& id, float size, AliveFlag alive);

// The image at `url`, `height` tall; hidden until it has downloaded, and for good if it cannot be.
brls::Box* remoteBanner(const std::string& url, float height, AliveFlag alive);

// Runs `work` on a new thread, then `done` on the UI thread if `alive` is still true.
template <typename Result>
void background(AliveFlag alive, std::function<Result()> work, std::function<void(Result)> done)
{
    runInBackground([alive, work = std::move(work), done = std::move(done)] {
        Result result = work();
        brls::sync([alive, done, result = std::move(result)] {
            if (*alive)
                done(result);
        });
    });
}

} // namespace pz::ui
