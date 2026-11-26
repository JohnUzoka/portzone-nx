#include "ui/widgets.hpp"

#include <cctype>
#include <utility>

#include "images.hpp"

namespace pz::ui
{

namespace
{

    struct Palette
    {
        NVGcolor background, panel, card, line, text, muted, accent, accent2, highlight, good, bad, onAccent;
    };

    const Palette DARK = {
        nvgRGB(17, 20, 29), nvgRGB(26, 30, 43), nvgRGB(31, 36, 52), nvgRGB(52, 59, 80), nvgRGB(236, 239, 246),
        nvgRGB(146, 155, 180), nvgRGB(124, 140, 255), nvgRGB(88, 216, 255), nvgRGB(40, 46, 66), nvgRGB(88, 214, 141),
        nvgRGB(255, 112, 112), nvgRGB(14, 16, 26),
    };

    const Palette LIGHT = {
        nvgRGB(242, 244, 249), nvgRGB(230, 233, 242), nvgRGB(255, 255, 255), nvgRGB(212, 216, 229), nvgRGB(26, 29, 41),
        nvgRGB(96, 104, 128), nvgRGB(78, 94, 228), nvgRGB(0, 138, 194), nvgRGB(255, 255, 255), nvgRGB(22, 146, 84),
        nvgRGB(212, 56, 56), nvgRGB(255, 255, 255),
    };

    const Palette& palette()
    {
        return brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT ? LIGHT : DARK;
    }

    void writeTheme(brls::Theme theme, const Palette& p)
    {
        theme.addColor("brls/clear", p.background);
        theme.addColor("brls/background", p.background);
        theme.addColor("brls/text", p.text);
        theme.addColor("brls/text_disabled", p.muted);
        theme.addColor("brls/accent", p.accent);
        theme.addColor("brls/click_pulse", nvgRGBAf(p.accent.r, p.accent.g, p.accent.b, 0.16f));
        theme.addColor("brls/highlight/background", p.highlight);
        theme.addColor("brls/highlight/color1", p.accent);
        theme.addColor("brls/highlight/color2", p.accent2);
        theme.addColor("brls/applet_frame/separator", p.line);
        theme.addColor("brls/sidebar/background", p.panel);
        theme.addColor("brls/sidebar/active_item", p.accent2);
        theme.addColor("brls/sidebar/separator", p.line);
        theme.addColor("brls/header/border", p.line);
        theme.addColor("brls/header/rectangle", p.accent);
        theme.addColor("brls/header/subtitle", p.muted);
        theme.addColor("brls/button/primary_enabled_background", p.accent);
        theme.addColor("brls/button/primary_enabled_text", p.onAccent);
        theme.addColor("brls/button/default_enabled_background", p.card);
        theme.addColor("brls/button/default_enabled_text", p.text);
        theme.addColor("brls/button/enabled_border_color", p.line);
        theme.addColor("brls/button/highlight_enabled_text", p.accent2);
        theme.addColor("brls/spinner/bar_color", nvgRGBAf(p.accent.r, p.accent.g, p.accent.b, 0.45f));
    }

    bool isInside(brls::View* view, brls::View* container)
    {
        for (; view; view = view->getParent())
            if (view == container)
                return true;
        return false;
    }

    // An image filling `size`, with rounded corners; nullptr (and a log line naming `url`) when it can't be shown.
    brls::Image* imageView(const images::Bytes& bytes, float radius, const std::string& url)
    {
        auto* image = new brls::Image();
        image->setImageFromMem(bytes->data(), int(bytes->size()));
        if (image->getTexture() == 0)
        {
            brls::Logger::warning("Could not show the image from {}", url);
            delete image;
            return nullptr;
        }
        image->setScalingType(brls::ImageScalingType::FILL);
        image->setClipsToBounds(true);
        image->setCornerRadius(radius);
        return image;
    }

} // namespace

void applyTheme(bool light)
{
    writeTheme(brls::Theme::getDarkTheme(), DARK);
    writeTheme(brls::Theme::getLightTheme(), LIGHT);
    brls::Application::getPlatform()->setThemeVariant(light ? brls::ThemeVariant::LIGHT : brls::ThemeVariant::DARK);
}

NVGcolor textColor() { return palette().text; }
NVGcolor mutedColor() { return palette().muted; }
NVGcolor goodColor() { return palette().good; }
NVGcolor badColor() { return palette().bad; }
NVGcolor accentColor() { return palette().accent2; }
NVGcolor cardColor() { return palette().card; }
NVGcolor stateColor(bool good, bool installed) { return good ? goodColor() : (installed ? badColor() : mutedColor()); }

brls::Label* label(const std::string& text, float size, NVGcolor color)
{
    auto* view = new brls::Label();
    view->setText(text);
    view->setFontSize(size);
    view->setTextColor(color);
    return view;
}

brls::Header* header(const std::string& title, const std::string& subtitle)
{
    auto* view = new brls::Header();
    view->setTitle(title);
    view->setSubtitle(subtitle);
    view->setMarginTop(24);
    view->setMarginBottom(12); // text right under the header's line would touch it
    // The title keeps its width; without this a long one is cut short even with room to spare.
    if (auto* titleLabel = dynamic_cast<brls::Label*>(view->getView("brls/header/title")))
        titleLabel->setShrink(0);
    return view;
}

brls::Button* button(const std::string& text, std::function<void()> onClick, bool primary)
{
    auto* view = new brls::Button();
    view->setText(text);
    view->setStyle(primary ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_BORDERED);
    view->setMarginRight(16);
    view->setShrink(0); // a squeezed button wraps its text onto two lines
    view->setPaddingLeft(24);
    view->setPaddingRight(24);
    view->registerClickAction([onClick = std::move(onClick)](brls::View*) {
        onClick();
        return true;
    });
    return view;
}

brls::Box* textRow(const std::string& text, float size, NVGcolor color, float indent)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setFocusable(true);
    row->setHideHighlightBackground(true);
    row->setPadding(4, 8, 4, 8 + indent);
    auto* content = label(text, size, color);
    content->setGrow(1);
    row->addView(content);
    return row;
}

brls::Box* textBlock(const std::string& text, float size, NVGcolor color)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    // Single-line labels are only as tall as their glyphs; without this, stacked lines touch.
    row->setPadding(3, 0, 3, 0);
    auto* content = label(text, size, color);
    content->setGrow(1);
    content->setShrink(1);
    row->addView(content);
    return row;
}

brls::Box* pill(const std::string& text, NVGcolor color)
{
    // Fixed heights: the label is centred on its own box, which is otherwise only as tall as its glyphs, so
    // "Easy Setup" (descenders) and "Manual" (none) would sit at different heights. nanovg centres on the font's
    // ascender/descender, which puts capitals 1.5 px high at this size: the 3 px top padding moves them down.
    constexpr float HEIGHT = 30;
    auto* box = new brls::Box(brls::Axis::ROW);
    box->setHeight(HEIGHT);
    box->setPadding(3, 14, 0, 14);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setCornerRadius(HEIGHT / 2);
    box->setBackgroundColor(nvgRGBAf(color.r, color.g, color.b, 0.16f));
    auto* caption = label(text, 15, color);
    caption->setHeight(HEIGHT - 3);
    caption->setVerticalAlign(brls::VerticalAlign::CENTER);
    box->addView(caption);
    // In a column the outer row stretches and the pill keeps the width of its text.
    auto* outer = new brls::Box(brls::Axis::ROW);
    outer->addView(box);
    return outer;
}

namespace
{

    // The initials tile's look, on an existing box.
    void styleTile(brls::Box* box, const std::string& title, const std::string& id, float size)
    {
        static const NVGcolor tints[] = { nvgRGB(124, 140, 255), nvgRGB(64, 196, 255), nvgRGB(255, 140, 105),
            nvgRGB(72, 204, 140), nvgRGB(232, 112, 214), nvgRGB(250, 186, 64), nvgRGB(160, 120, 255), nvgRGB(255, 99, 132) };
        uint32_t hash = 2166136261u; // FNV-1a: neighbouring ids still land on different tints
        for (unsigned char c : id)
            hash = (hash ^ c) * 16777619u;
        const NVGcolor tint = tints[hash % (sizeof(tints) / sizeof(tints[0]))];

        // First letters of the first two words that start with a letter or digit.
        std::string initials;
        bool wordStart = true;
        for (char c : title)
        {
            const bool alnum = std::isalnum(static_cast<unsigned char>(c));
            if (wordStart && alnum && initials.size() < 2)
                initials += char(std::toupper(static_cast<unsigned char>(c)));
            wordStart = !alnum;
        }

        box->setWidth(size);
        box->setHeight(size);
        box->setShrink(0);
        box->setCornerRadius(size * 0.24f);
        box->setBackgroundColor(nvgRGBAf(tint.r, tint.g, tint.b, 0.22f));
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        box->addView(label(initials, size * 0.36f, tint));
    }

    // The initials tile until its first draw; then it fetches its image and shows it once it is there.
    class RemoteIcon : public brls::Box
    {
      public:
        RemoteIcon(std::string url, float size, AliveFlag alive)
            : brls::Box(brls::Axis::ROW)
            , url(std::move(url))
            , size(size)
            , alive(std::move(alive))
        {
        }

        void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
            brls::FrameContext* ctx) override
        {
            if (!url.empty())
                fetch(std::exchange(url, {}));
            brls::Box::draw(vg, x, y, width, height, style, ctx);
        }

      private:
        void fetch(const std::string& from)
        {
            ui::background<images::Bytes>(alive, [from] { return images::get(from, 1 << 20); },
                [this, from, size = size](images::Bytes bytes) {
                    auto* image = bytes ? imageView(bytes, size * 0.24f, from) : nullptr;
                    if (!image)
                        return; // keep the initials
                    clearViews();
                    setBackgroundColor(nvgRGBA(0, 0, 0, 0));
                    image->setWidth(size);
                    image->setHeight(size);
                    addView(image);
                });
        }

        std::string url; // emptied once the download has started
        float size;
        AliveFlag alive; // the owner's: this view lives as long as it does
    };

} // namespace

void CulledBox::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
    brls::FrameContext* ctx)
{
    const float top = getY(), left = getX(), bottom = top + getHeight(), right = left + getWidth();
    for (brls::Box* bounds = getParent(); bounds; bounds = bounds->getParent())
    {
        float boundsTop, boundsRight, boundsBottom, boundsLeft;
        bounds->getCullingBounds(&boundsTop, &boundsRight, &boundsBottom, &boundsLeft);
        if (bottom < boundsTop || right < boundsLeft || left > boundsRight || top > boundsBottom)
            return;
    }
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

brls::Box* tile(const std::string& title, const std::string& id, float size)
{
    auto* box = new brls::Box(brls::Axis::ROW);
    styleTile(box, title, id, size);
    return box;
}

brls::Box* remoteIcon(const std::string& url, const std::string& title, const std::string& id, float size, AliveFlag alive)
{
    auto* box = new RemoteIcon(url, size, std::move(alive));
    styleTile(box, title, id, size);
    return box;
}

brls::Box* remoteBanner(const std::string& url, float height, AliveFlag alive)
{
    auto* box = new brls::Box(brls::Axis::ROW);
    box->setVisibility(brls::Visibility::GONE);
    if (url.empty())
        return box;
    background<images::Bytes>(alive, [url] { return images::get(url, 2 << 20); }, [box, height, url](images::Bytes bytes) {
        auto* image = bytes ? imageView(bytes, 12, url) : nullptr;
        if (!image)
            return;
        image->setGrow(1);
        image->setHeight(height);
        box->addView(image);
        box->setVisibility(brls::Visibility::VISIBLE);
    });
    return box;
}

brls::Box* card(brls::Axis axis)
{
    auto* box = new brls::Box(axis);
    box->setCornerRadius(14);
    box->setBackgroundColor(cardColor());
    box->setPadding(18, 20, 18, 20);
    return box;
}

brls::Box* focusParking()
{
    auto* view = new brls::Box();
    view->setFocusable(true);
    view->setHideHighlight(true);
    view->setWidth(1);
    view->setHeight(1);
    return view;
}

void rebuild(brls::Box* box, brls::View* fallbackFocus, const std::function<void(brls::Box*)>& fill)
{
    // borealis keeps a pointer to the focused view: move focus out before its view is deleted.
    const bool hadFocus = isInside(brls::Application::getCurrentFocus(), box);
    if (hadFocus)
        brls::Application::giveFocus(fallbackFocus);
    box->clearViews();
    fill(box);
    // Unless `fill` already chose where focus goes.
    if (hadFocus && brls::Application::getCurrentFocus() == fallbackFocus)
        if (brls::View* first = box->getDefaultFocus())
            brls::Application::giveFocus(first);
}

} // namespace pz::ui
