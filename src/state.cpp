#include "state.hpp"

#include <borealis/core/i18n.hpp>

using namespace brls::literals;

namespace pz
{

PortState portState(const Port& port)
{
    PortState state;
    state.record = loadRecord(port.info.id);
    if (port.manifest)
    {
        std::error_code ec;
        state.installed = fs::exists(sdPath(port.manifest->binary), ec);
        if (port.manifest->gameData)
            state.data = checkGameData(*port.manifest->gameData);
    }
    else
        state.installed = state.record.has_value();
    return state;
}

std::set<std::string> installedIds(const std::vector<Port>& ports)
{
    // installed/<id>.json, one per port PortZone installed (installer.cpp's recordPath).
    std::set<std::string> recorded;
    std::error_code ec;
    for (fs::directory_iterator it(dataDir() / "installed", ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".json")
            recorded.insert(it->path().stem().string());

    std::set<std::string> out;
    for (const Port& port : ports)
    {
        std::error_code existsError;
        if (port.manifest ? fs::exists(sdPath(port.manifest->binary), existsError) : recorded.count(port.info.id) > 0)
            out.insert(port.info.id);
    }
    return out;
}

bool stateGood(const Port& port, const PortState& state)
{
    return state.installed && (!state.data || state.data->ready);
}

std::string stateSummary(const Port& port, const PortState& state)
{
    if (!state.installed && !port.runsHere())
        return brls::getStr("portzone/state/for_only", consolesLabel(port.runsOn()));
    if (!port.manifest)
        return state.installed ? brls::getStr("portzone/state/installed_tag", state.record->tag) :
                                 "portzone/state/not_installed"_i18n;
    if (!state.installed)
    {
        if (port.section() == Section::BuildOnPc)
            return "portzone/state/build_first"_i18n;
        if (port.section() == Section::PrepareOnPc && !(state.data && state.data->ready))
            return "portzone/state/prepare_files"_i18n;
        return state.data && state.data->ready ? "portzone/state/files_ready"_i18n : "portzone/state/not_installed"_i18n;
    }
    if (state.data && !state.data->ready)
        return "portzone/state/files_missing_long"_i18n;
    return "portzone/state/ready_to_play"_i18n;
}

std::string stateShort(const Port& port, const PortState& state)
{
    if (!state.installed)
    {
        if (!port.runsHere())
            return brls::getStr("portzone/state/only", consolesLabel(port.runsOn()));
        if (port.manifest && port.section() == Section::BuildOnPc)
            return "portzone/state/build_on_pc"_i18n;
        return port.manifest && port.section() == Section::PrepareOnPc ? "portzone/state/prepare_on_pc"_i18n :
                                                                         "portzone/state/not_installed"_i18n;
    }
    if (state.data && !state.data->ready)
        return "portzone/state/files_missing"_i18n;
    return port.manifest ? "portzone/state/ready"_i18n : "portzone/state/installed"_i18n;
}

std::string consolesLabel(const std::vector<std::string>& consoles)
{
    std::string text;
    for (size_t i = 0; i < consoles.size(); i++)
    {
        const std::string name = consoles[i] == "switch2" ? "Switch 2" : (consoles[i] == "switch" ? "Switch" : consoles[i]);
        if (i == 0)
            text = name;
        else
            text = brls::getStr(i + 1 == consoles.size() ? "portzone/common/list_and" : "portzone/common/list_comma", text, name);
    }
    return text;
}

} // namespace pz
