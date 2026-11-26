#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#ifdef PORTZONE_EMULATOR_BUILD
#include <switch.h>
#endif

#include "config.hpp"
#include "http.hpp"
#include "settings.hpp"
#include "ui/home.hpp"

int main(int argc, char* argv[])
{
    const std::string argsError = pz::parseArgs(argc, argv);
    if (!argsError.empty())
    {
        std::fprintf(stderr, "%s\n", argsError.c_str());
        return EXIT_FAILURE;
    }
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "-d")
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
#ifdef PORTZONE_EMULATOR_BUILD
    // Ryujinx prints svcOutputDebugString, where libnx sends stderr: the log goes to its console.
    consoleDebugInit(debugDevice_SVC);
    brls::Logger::setLogOutput(stderr);
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
#endif

    pz::http::init();
    pz::loadSettings();
    if (!pz::config().consoleFromArgs && !pz::settings().console.empty())
        pz::config().console = pz::settings().console;

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;
    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init borealis");
        return EXIT_FAILURE;
    }
    brls::Application::createWindow("PortZone");
    pz::ui::applyTheme(pz::settings().lightTheme);
    brls::Application::setGlobalQuit(true);

    brls::Application::pushActivity(new pz::ui::HomeActivity());
    while (brls::Application::mainLoop())
        ;

    pz::http::cleanup();
    return EXIT_SUCCESS;
}
