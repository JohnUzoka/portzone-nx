#include "state.hpp"

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

bool stateGood(const Port& port, const PortState& state)
{
    return state.installed && (!state.data || state.data->ready);
}

std::string stateSummary(const Port& port, const PortState& state)
{
    if (!state.installed && !port.runsHere())
        return "For " + consolesLabel(port.runsOn()) + " only";
    if (!port.manifest)
        return state.installed ? "Installed " + state.record->tag : "Not installed";
    if (!state.installed)
    {
        if (port.section() == Section::BuildOnPc)
            return "Build on a PC first";
        return state.data && state.data->ready ? "Files ready · not installed" : "Not installed";
    }
    if (state.data && !state.data->ready)
        return "Installed · your files are missing";
    return "Ready to play";
}

std::string stateShort(const Port& port, const PortState& state)
{
    if (!state.installed)
    {
        if (!port.runsHere())
            return consolesLabel(port.runsOn()) + " only";
        return port.manifest && port.section() == Section::BuildOnPc ? "Build on PC" : "Not installed";
    }
    if (state.data && !state.data->ready)
        return "Files missing";
    return port.manifest ? "Ready" : "Installed";
}

std::string consolesLabel(const std::vector<std::string>& consoles)
{
    std::string text;
    for (size_t i = 0; i < consoles.size(); i++)
    {
        if (i > 0)
            text += i + 1 == consoles.size() ? " and " : ", ";
        text += consoles[i] == "switch2" ? "Switch 2" : (consoles[i] == "switch" ? "Switch" : consoles[i]);
    }
    return text;
}

} // namespace pz
