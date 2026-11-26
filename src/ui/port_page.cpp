#include "ui/port_page.hpp"

#include <algorithm>
#include <memory>

#include "catalog.hpp"
#include "config.hpp"
#include "markdown.hpp"
#include "settings.hpp"
#include "state.hpp"
#include "ui/sdsearch_page.hpp"
#include "ui/transfer_page.hpp"

namespace pz::ui
{

namespace
{

    constexpr float INDENT = 28;

    std::string join(const std::vector<std::string>& parts, const std::string& separator)
    {
        std::string out;
        for (const auto& part : parts)
            out += (out.empty() ? "" : separator) + part;
        return out;
    }

    std::string fileText(const FileSpec& file)
    {
        return file.match.empty() ? file.path : file.path + file.match;
    }

    std::string filesText(const Option& option)
    {
        std::vector<std::string> names;
        for (const auto& file : option.files)
            names.push_back(fileText(file));
        return join(names, ", ");
    }

    // A row of the layout tree: name, note, state.
    brls::Box* treeRow(const std::string& name, int depth, const std::string& note, const std::string& state, NVGcolor stateColor)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setHideHighlightBackground(true);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(3, 8, 3, 8 + depth * INDENT);
        auto* nameLabel = label(name, 19);
        nameLabel->setWidth(std::max(180.0f, 360.0f - depth * INDENT));
        nameLabel->setSingleLine(true);
        row->addView(nameLabel);
        auto* noteLabel = label(note, 17, mutedColor());
        noteLabel->setGrow(1);
        noteLabel->setShrink(1);
        noteLabel->setMarginLeft(12);
        row->addView(noteLabel);
        auto* stateLabel = label(state, 17, stateColor);
        stateLabel->setWidth(220);
        stateLabel->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        row->addView(stateLabel);
        return row;
    }

    struct TreeNode
    {
        std::string name;
        bool folder = false;
        std::vector<std::unique_ptr<TreeNode>> children;
        bool hasEntry = false;
        std::string note, state;
        NVGcolor color {};
    };

    void insert(TreeNode& root, const std::string& path, const std::string& note, const std::string& state, NVGcolor color)
    {
        TreeNode* node = &root;
        size_t start   = 0;
        while (start < path.size())
        {
            size_t slash      = path.find('/', start);
            const bool last   = slash == std::string::npos || slash + 1 == path.size();
            const bool folder = slash != std::string::npos;
            if (slash == std::string::npos)
                slash = path.size();
            const std::string part = path.substr(start, slash - start);
            start                  = slash + 1;

            TreeNode* next = nullptr;
            for (auto& child : node->children)
                if (lower(child->name) == lower(part) && child->folder == folder)
                    next = child.get();
            if (!next)
            {
                node->children.push_back(std::make_unique<TreeNode>());
                next         = node->children.back().get();
                next->name   = part;
                next->folder = folder;
            }
            node = next;
            if (last && !node->hasEntry)
            {
                node->hasEntry = true;
                node->note     = note;
                node->state    = state;
                node->color    = color;
            }
        }
    }

    void render(const TreeNode& node, int depth, brls::Box* box)
    {
        for (const auto& child : node.children)
        {
            box->addView(treeRow(child->name + (child->folder ? "/" : ""), depth, child->note, child->state,
                child->hasEntry ? child->color : mutedColor()));
            render(*child, depth + 1, box);
        }
    }

    bool isNroOrZip(const std::string& name) { return endsWithNoCase(name, ".nro") || endsWithNoCase(name, ".zip"); }

    // Switch builds first, then the rest by name.
    int assetRank(const std::string& name)
    {
        const std::string n = lower(name);
        if (endsWithNoCase(n, ".nro"))
            return 0;
        if (n.find("switch") != std::string::npos || n.find("-nx") != std::string::npos || n.find("_nx") != std::string::npos)
            return 1;
        return 2;
    }

    std::vector<std::string> assetSuffixes(const Manifest& manifest)
    {
        std::vector<std::string> suffixes;
        for (const auto& step : manifest.installSteps)
            suffixes.push_back(step.asset);
        return suffixes;
    }

    // portzone.json's steps with the release's files.
    InstallPlan manifestPlan(const std::string& id, const Manifest& manifest, const github::Picked& picked)
    {
        InstallPlan plan { id, picked.release.tag, {} };
        for (size_t i = 0; i < manifest.installSteps.size(); i++)
        {
            const InstallStep& step = manifest.installSteps[i];
            PlanStep planned;
            planned.asset = picked.assets[i];
            planned.unzip = step.kind == InstallStep::Kind::Unzip;
            planned.dest  = step.dest;
            planned.from  = step.from;
            planned.name  = step.name;
            plan.steps.push_back(std::move(planned));
        }
        return plan;
    }

    // A file the player picked for a port without portzone.json: a zip is unzipped the way Sphaira would.
    InstallPlan manualPlan(const std::string& id, const github::Release& release, const github::Asset& asset,
        const std::string& dest)
    {
        PlanStep step;
        step.asset       = asset;
        step.unzip       = endsWithNoCase(asset.name, ".zip");
        step.dest        = dest;
        step.guessLayout = step.unzip;
        return { id, release.tag, { step } };
    }

} // namespace

PortActivity::PortActivity(std::string id)
    : id(std::move(id))
{
}

PortActivity::~PortActivity() { *alive = false; }

const Port& PortActivity::port() const { return *Catalog::get().find(id); }

bool PortActivity::testBuilds() const
{
    const Port& p = port();
    return (p.manifest && p.manifest->prerelease) || settings().testBuilds.count(id) > 0;
}

brls::View* PortActivity::createContentView()
{
    const Port& p = port();

    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    content->setPadding(24, 48, 48, 48);

    // Hero card: banner, icon, title, author, kind of setup, description.
    intro = card(brls::Axis::COLUMN);
    intro->setFocusable(true);
    intro->setHideHighlightBackground(true);
    auto* banner = remoteBanner(p.bannerUrl(), 300, alive);
    banner->setMarginBottom(18);
    intro->addView(banner);
    auto* top = new brls::Box(brls::Axis::ROW);
    top->setAlignItems(brls::AlignItems::CENTER);
    auto* icon = remoteIcon(p.iconUrl(), p.info.title, p.info.id, 96, alive);
    icon->setMarginRight(20);
    top->addView(icon);
    auto* heading = new brls::Box(brls::Axis::COLUMN);
    heading->setGrow(1);
    heading->setShrink(1);
    heading->addView(textBlock(p.info.title, 30));
    heading->addView(textBlock("by " + p.info.author + "  ·  " + p.info.url.substr(8), 16, mutedColor()));
    auto* tags = new brls::Box(brls::Axis::ROW);
    tags->setMarginTop(6);
    const char* kind = p.section() == Section::EasySetup ? "Easy Setup" :
        (p.section() == Section::BuildOnPc ? "Build on PC" : "Manual install");
    tags->addView(pill(kind, accentColor()));
    if (!p.originalPlatform().empty())
    {
        const std::string& from = p.originalPlatform();
        auto* system            = pill(from == "Source" ? "Built from source code" : "From " + from, mutedColor());
        system->setMarginLeft(8);
        tags->addView(system);
    }
    if (!p.runsHere() || p.runsOn().size() > 1)
    {
        auto* consoles = pill(consolesLabel(p.runsOn()), p.runsHere() ? mutedColor() : badColor());
        consoles->setMarginLeft(8);
        tags->addView(consoles);
    }
    heading->addView(tags);
    top->addView(heading);
    intro->addView(top);
    auto* description = textBlock(p.description(), 20);
    description->setMarginTop(14);
    intro->addView(description);
    content->addView(intro);

    statusBox = new brls::Box(brls::Axis::COLUMN);
    statusBox->setAlignItems(brls::AlignItems::STRETCH);
    statusBox->setMarginTop(18);
    content->addView(statusBox);

    if (p.manifest)
    {
        if (p.manifest->gameData)
        {
            const std::string folder = sdShown(p.manifest->gameData->dir);
            content->addView(header("What you need", folder.empty() ? "Put these on the SD card" : "Put these in " + folder + "/"));
            needsBox = new brls::Box(brls::Axis::COLUMN);
            needsBox->setAlignItems(brls::AlignItems::STRETCH);
            content->addView(needsBox);

            content->addView(header("Folder layout", folder.empty() ? "SD card" : folder + "/"));
            layoutBox = new brls::Box(brls::Axis::COLUMN);
            layoutBox->setAlignItems(brls::AlignItems::STRETCH);
            content->addView(layoutBox);
        }

        content->addView(header("Steps"));
        int n = 1;
        for (const auto& step : p.manifest->steps)
            content->addView(textRow(std::to_string(n++) + ".  " + step));

        std::vector<std::string> notes;
        if (p.manifest->fullMemory)
            notes.push_back("Start it with full memory: hold R while starting a game, or use a forwarder. Not from the Album.");
        notes.insert(notes.end(), p.manifest->launchNotes.begin(), p.manifest->launchNotes.end());
        if (!notes.empty())
        {
            content->addView(header("Launching"));
            for (const auto& note : notes)
                content->addView(textRow("•  " + note));
        }
    }
    else
    {
        content->addView(header("Release files", "Pick one to download and extract"));
        releaseBox = new brls::Box(brls::Axis::COLUMN);
        releaseBox->setAlignItems(brls::AlignItems::STRETCH);
        content->addView(releaseBox);

        content->addView(header("README", "From the port's repository. Follow it for where your game files go."));
        readmeBox = new brls::Box(brls::Axis::COLUMN);
        readmeBox->setAlignItems(brls::AlignItems::STRETCH);
        content->addView(readmeBox);
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setContentView(content);
    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle(p.info.title);
    return frame;
}

void PortActivity::onContentAvailable()
{
    refresh();
    if (releaseBox)
        rebuild(releaseBox, intro, [this](brls::Box* box) { fillReleaseFiles(box); });
    if (readmeBox)
        rebuild(readmeBox, intro, [this](brls::Box* box) { fillReadme(box); });

    const Port& p = port();
    if (!p.manifest || !p.manifest->installSteps.empty())
        background<github::Releases>(alive, [url = p.info.url] { return github::fetchReleases(url); },
            [this](github::Releases result) {
                releases = std::move(result);
                refresh();
            });
    if (!p.manifest)
        background<github::Readme>(alive, [url = p.info.url] { return github::fetchReadme(url); },
            [this](github::Readme result) {
                readme = std::move(result);
                rebuild(readmeBox, intro, [this](brls::Box* box) { fillReadme(box); });
            });

    if (auto running = jobFor(id); running && !running->finished)
        watch(running);

    // The store's copy of portzone.json is up to a day old: a newer one in the repository rebuilds this page.
    background<std::shared_ptr<std::optional<Port>>>(alive,
        [copy = p] { return std::make_shared<std::optional<Port>>(Catalog::fresher(copy)); },
        [this, id = id](std::shared_ptr<std::optional<Port>> fresh) {
            if (!*fresh || (job && !job->finished))
                return;
            Catalog::get().replace(std::move(**fresh));
            brls::Application::popActivity(brls::TransitionAnimation::NONE,
                [id] { brls::Application::pushActivity(new PortActivity(id), brls::TransitionAnimation::NONE); });
        });
}

void PortActivity::refresh()
{
    rebuild(statusBox, intro, [this](brls::Box* box) { fillStatus(box); });
    if (needsBox)
        rebuild(needsBox, intro, [this](brls::Box* box) { fillNeeds(box); });
    if (layoutBox)
        rebuild(layoutBox, intro, [this](brls::Box* box) { fillLayout(box); });
    if (releaseBox)
        rebuild(releaseBox, intro, [this](brls::Box* box) { fillReleaseFiles(box); });
}

void PortActivity::fillStatus(brls::Box* box)
{
    const Port& p          = port();
    const PortState state  = portState(p);
    const bool busy        = job && !job->finished;
    const auto* releaseSet = releases ? &releases->releases : nullptr;

    if (!p.manifestError.empty())
        box->addView(textBlock(p.manifestError + ". Showing the README instead.", 18, badColor()));

    auto* stateRow = new brls::Box(brls::Axis::ROW);
    stateRow->setAlignItems(brls::AlignItems::CENTER);
    stateRow->addView(pill(stateSummary(p, state), stateColor(stateGood(p, state), state.installed)));
    std::string installedBy;
    if (state.record)
        installedBy = "PortZone installed " + state.record->tag;
    else if (state.installed && p.manifest)
        installedBy = "Installed outside PortZone";
    if (!installedBy.empty())
    {
        auto* note = label(installedBy, 16, mutedColor());
        note->setMarginLeft(14);
        stateRow->addView(note);
    }
    box->addView(stateRow);

    if (!p.runsHere())
    {
        auto* hint = textBlock("This port's release runs on " + consolesLabel(p.runsOn()) +
                ". This copy of PortZone is for the " + consolesLabel({ config().console }) + ", so it does not install it.",
            18, mutedColor());
        hint->setMarginTop(10);
        box->addView(hint);
    }

    auto* actions = new brls::Box(brls::Axis::ROW);
    actions->setMarginTop(14);
    actions->setAlignItems(brls::AlignItems::CENTER);

    if (busy)
        actions->addView(button("Cancel download", [this] {
            if (job)
                job->cancel = true;
        }));
    else if (p.runsHere() && p.manifest && !p.manifest->installSteps.empty())
    {
        const Manifest& manifest = *p.manifest;
        if (!releases)
            actions->addView(label("Checking GitHub for releases...", 18, mutedColor()));
        else if (!releases->error.empty())
            actions->addView(label(releases->error, 18, badColor()));
        else if (auto picked = github::pickRelease(*releaseSet, assetSuffixes(manifest), testBuilds()))
        {
            const InstallPlan plan = manifestPlan(id, manifest, *picked);
            const std::string tag  = picked->release.tag;
            uint64_t bytes         = 0;
            for (const auto& asset : picked->assets)
                bytes += asset.size;
            const std::string size = "  (" + formatSize(bytes) + ")";
            if (state.record && state.record->tag == tag)
                actions->addView(button("Reinstall " + tag, [this, plan] { install(plan); }));
            else if (state.installed)
                actions->addView(button("Update to " + tag + size, [this, plan] { install(plan); }, true));
            else if (p.section() == Section::BuildOnPc)
            {
                auto* hint = textBlock("Make the game files on a PC first (see Steps): the zip it makes includes the game.", 18, mutedColor());
                hint->setMarginTop(10);
                box->addView(hint);
            }
            else
                actions->addView(button("Install " + tag + size, [this, plan] { install(plan); }, true));
        }
        else
        {
            std::vector<std::string> names;
            for (const auto& suffix : assetSuffixes(manifest))
                names.push_back("\"" + suffix + "\"");
            actions->addView(label("No release has files ending in " + join(names, " and ") + ".", 18, badColor()));
        }
    }
    else if (p.runsHere() && !p.manifest && state.record && releases && releases->error.empty())
    {
        if (const auto* latest = github::latest(*releaseSet, testBuilds()); latest && latest->tag != state.record->tag)
            for (const auto& asset : latest->assets)
                if (asset.name == state.record->asset)
                {
                    actions->addView(button("Update to " + latest->tag,
                        [this, plan = manualPlan(id, *latest, asset, state.record->target)] { install(plan); }, true));
                }
    }

    // A toggle only when there are test builds to take and the port doesn't already use them.
    const bool hasPrereleases = releaseSet && std::any_of(releaseSet->begin(), releaseSet->end(),
                                                  [](const github::Release& r) { return r.prerelease; });
    if (hasPrereleases && p.runsHere() && !busy && !(p.manifest && p.manifest->prerelease))
        actions->addView(button(std::string("Test builds: ") + (testBuilds() ? "On" : "Off"), [this] {
            auto& chosen = settings().testBuilds;
            if (!chosen.erase(id))
                chosen.insert(id);
            saveSettings();
            refresh();
        }));
    if (state.record && !busy)
        actions->addView(button("Remove", [this] { confirmRemove(); }));
    if (p.manifest && p.manifest->gameData && !busy)
        actions->addView(button("Check files again", [this] { refresh(); }));
    // Without game data PortZone can't tell where files go; the upload page lets the player put them where the README says.
    if (!(p.manifest && p.manifest->gameData))
        actions->addView(button("Send files from phone or PC",
            [this] { brls::Application::pushActivity(new TransferActivity(id)); }));
    if (!actions->getChildren().empty())
        box->addView(actions);
    else
        delete actions;

    progress = label("", 18, accentColor());
    progress->setMarginTop(8);
    if (job)
    {
        const std::string error = job->error();
        if (!error.empty())
            progress->setTextColor(badColor());
        progress->setText(!error.empty() ? error : job->stage());
    }
    box->addView(progress);
}

void PortActivity::fillNeeds(brls::Box* box)
{
    const GameData& data = *port().manifest->gameData;
    const DataCheck check = checkGameData(data);

    for (const auto& input : check.inputs)
    {
        std::string state = input.met ? "Ready" : (input.input->required ? "Missing" : "Optional");
        NVGcolor color    = input.met ? goodColor() : (input.input->required ? badColor() : mutedColor());
        if (input.metOption < 0 && input.met)
            state = "Converted";

        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setHideHighlightBackground(true);
        row->setPadding(8, 8, 2, 8);
        auto* title = label(input.input->label, 20);
        title->setGrow(1);
        title->setShrink(1);
        row->addView(title);
        auto* stateLabel = label(state, 18, color);
        stateLabel->setWidth(160);
        stateLabel->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        row->addView(stateLabel);
        box->addView(row);

        const bool several = input.options.size() > 1;
        if (several)
            box->addView(textRow("Any one of:", 17, mutedColor(), INDENT));
        for (const auto& option : input.options)
        {
            std::string text = (several ? option.option->label + ":  " : "") + filesText(*option.option);
            std::vector<std::string> details;
            size_t found = 0;
            for (const auto& file : option.files)
            {
                found += file.found;
                if (!file.detail.empty())
                    details.push_back(file.detail);
            }
            if (option.met)
                details.push_back("found");
            else if (found > 0)
                details.push_back(std::to_string(found) + " of " + std::to_string(option.files.size()) + " found");
            if (!details.empty())
                text += "  (" + join(details, ", ") + ")";
            box->addView(textRow("•  " + text, 18, option.met ? goodColor() : textColor(), INDENT));
            if (!option.option->url.empty())
                box->addView(textRow("Get it free: " + option.option->url, 16, mutedColor(), INDENT * 2));
        }
    }

    if (data.prepare)
    {
        const Prepare& prep = *data.prepare;
        std::string text    = prep.where == "pc" ? "On a PC" : "On the Switch";
        if (!prep.toolName.empty())
            text += ", with " + prep.toolName + " (" + prep.toolUrl + ")";
        box->addView(textRow(text + ": " + prep.summary, 18, textColor()));
        if (check.converted)
            box->addView(textRow("Already done: " + join(prep.done, ", ") + " found.", 18, goodColor(), INDENT));
    }

    if (!data.inputs.empty())
    {
        auto* actions = new brls::Box(brls::Axis::ROW);
        actions->setMarginTop(14);
        actions->addView(button("Send from phone or PC",
            [this] { brls::Application::pushActivity(new TransferActivity(id)); }, true));
        actions->addView(button("Find on SD card", [this] { brls::Application::pushActivity(new SdSearchActivity(id)); }));
        box->addView(actions);
    }
}

void PortActivity::fillLayout(brls::Box* box)
{
    const Port& p         = port();
    const GameData& data  = *p.manifest->gameData;
    const DataCheck check = checkGameData(data);
    TreeNode root;

    const std::string dir = sdShown(data.dir).empty() ? "" : sdShown(data.dir) + "/";
    const std::string bin = sdShown(p.manifest->binary);
    if (lower(bin).rfind(lower(dir), 0) == 0)
    {
        std::error_code ec;
        const bool installed = fs::exists(sdPath(p.manifest->binary), ec);
        insert(root, bin.substr(dir.size()), "The game", installed ? "Installed" : "Not installed",
            installed ? goodColor() : badColor());
    }

    for (const auto& input : check.inputs)
    {
        const auto& shown = input.options[input.metOption >= 0 ? size_t(input.metOption) : 0];
        for (const auto& file : shown.files)
        {
            std::string note = file.spec->note.empty() ? shown.option->label : file.spec->note;
            if (!file.spec->match.empty())
                note = file.spec->match + "  " + note;
            if (input.options.size() > 1 && input.metOption < 0)
                note += " (or another option above)";
            std::string state;
            NVGcolor color;
            if (file.found)
            {
                // A matched name (for *.3ds) is long: it goes with the note; counts ("41 files") stay short.
                const bool matchedName = file.spec->path.find_first_of("*?") != std::string::npos;
                if (matchedName && !file.detail.empty())
                    note += ": " + file.detail;
                state = file.detail.empty() || matchedName ? "Found" : "Found · " + file.detail;
                color = goodColor();
            }
            else if (input.met)
            {
                state = "Not needed now";
                color = mutedColor();
            }
            else if (!input.input->required)
            {
                state = "Optional";
                color = mutedColor();
            }
            else
            {
                state = file.detail.empty() ? "Missing" : file.detail;
                color = badColor();
            }
            insert(root, file.spec->path, note, state, color);
        }
    }

    for (const auto& generated : data.generated)
    {
        const bool here = present(data, generated.path);
        insert(root, generated.path, generated.note, here ? "Present" : "Made by the game", mutedColor());
    }
    if (data.prepare)
        for (const auto& done : data.prepare->done)
        {
            const bool here = present(data, done);
            insert(root, done, "Made from your files", here ? "Present" : "Made on first start", here ? goodColor() : mutedColor());
        }

    render(root, 0, box);
}

void PortActivity::fillReleaseFiles(brls::Box* box)
{
    if (!releases)
    {
        box->addView(textRow("Checking GitHub for releases...", 18, mutedColor()));
        return;
    }
    if (!releases->error.empty())
    {
        box->addView(textRow(releases->error, 18, badColor()));
        return;
    }
    const github::Release* release = github::latest(releases->releases, testBuilds());
    if (!release)
    {
        box->addView(textRow("This repository has no releases.", 18, mutedColor()));
        return;
    }

    box->addView(textRow(release->tag + (release->name.empty() || release->name == release->tag ? "" : "  ·  " + release->name) +
            (release->prerelease ? "  (prerelease)" : ""),
        19));

    // A release for many systems: only the files that look like Switch builds.
    bool anySwitch = false;
    for (const auto& asset : release->assets)
        anySwitch |= isNroOrZip(asset.name) && assetRank(asset.name) < 2;
    std::vector<github::Asset> assets;
    size_t other = 0;
    for (const auto& asset : release->assets)
        if (isNroOrZip(asset.name) && (!anySwitch || assetRank(asset.name) < 2))
            assets.push_back(asset);
        else
            other++;
    std::stable_sort(assets.begin(), assets.end(), [](const auto& a, const auto& b) { return assetRank(a.name) < assetRank(b.name); });

    const bool busy = job && !job->finished;
    for (const auto& asset : assets)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setMarginTop(8);
        auto* name = label(asset.name + "  ·  " + formatSize(asset.size), 18);
        name->setGrow(1);
        name->setShrink(1);
        row->addView(name);
        if (!busy && port().runsHere())
        {
            const github::Release chosen = *release;
            row->addView(button("Install", [this, chosen, asset] {
                const std::string base                 = port().defaultFolder();
                const std::vector<std::string> targets = { base, "sdmc:/switch", "sdmc:/" };
                // Install once the dropdown has closed, so the page can rebuild without the dropdown's focus.
                auto* dropdown = new brls::Dropdown(
                    "Where should " + asset.name + " go?",
                    { sdShown(base) + "/  (suggested)", "switch/", "The SD card's root" },
                    [](int) {}, 0,
                    [this, chosen, asset, targets](int selected) {
                        if (selected >= 0 && selected < int(targets.size()))
                            install(manualPlan(id, chosen, asset, targets[size_t(selected)]));
                    });
                brls::Application::pushActivity(new brls::Activity(dropdown));
            }));
        }
        box->addView(row);
    }
    if (assets.empty())
        box->addView(textRow("This release has no .nro or .zip file.", 18, mutedColor()));
    if (other)
        box->addView(textRow(std::to_string(other) + " other file" + (other == 1 ? "" : "s") +
                " for other systems not shown. A zip with a switch/ folder inside always extracts to the SD card's root.",
            16, mutedColor()));
}

void PortActivity::fillReadme(brls::Box* box)
{
    if (!readme)
    {
        box->addView(textRow("Loading the README...", 18, mutedColor()));
        return;
    }
    if (!readme->error.empty())
    {
        box->addView(textRow("Could not load the README: " + readme->error, 18, badColor()));
        return;
    }
    for (const auto& block : parseMarkdown(readme->text))
    {
        switch (block.kind)
        {
            case TextBlock::Kind::Heading:
            {
                auto* row = textRow(block.text, block.level <= 1 ? 26 : (block.level == 2 ? 23 : 21), accentColor());
                row->setMarginTop(14);
                box->addView(row);
                break;
            }
            case TextBlock::Kind::Paragraph:
                box->addView(textRow(block.text, 18));
                break;
            case TextBlock::Kind::Bullet:
                box->addView(textRow("•  " + block.text, 18, textColor(), INDENT * float(block.level + 1)));
                break;
            case TextBlock::Kind::Code:
            {
                auto* row = textRow(block.text, 16, mutedColor(), INDENT);
                row->setBackgroundColor(brls::Application::getTheme()["brls/sidebar/background"]);
                box->addView(row);
                break;
            }
        }
    }
}

void PortActivity::install(InstallPlan plan)
{
    if (auto started = startInstall(std::move(plan)))
        watch(started);
    else
        brls::Application::notify("Already working on this port.");
}

void PortActivity::confirmRemove()
{
    auto* dialog = new brls::Dialog("Remove " + port().info.title +
        "? Only the files PortZone installed are deleted; your game files and saves stay.");
    dialog->addButton("Cancel", [] {});
    dialog->addButton("Remove", [this] {
        if (auto started = startRemove(id))
            watch(started);
    });
    dialog->open();
}

void PortActivity::watch(std::shared_ptr<Job> running)
{
    job = std::move(running);
    refresh();
    poll(job);
}

void PortActivity::poll(std::shared_ptr<Job> watched)
{
    brls::delay(200, [this, alive = alive, watched] {
        if (!*alive || job != watched)
            return;
        if (watched->finished)
        {
            refresh();
            const std::string error = watched->error();
            brls::Application::notify(error.empty() ? watched->stage() : error);
            return;
        }
        if (progress)
            progress->setText(watched->stage() + "  " + std::to_string(int(watched->fraction * 100)) + "%");
        poll(watched);
    });
}

} // namespace pz::ui
