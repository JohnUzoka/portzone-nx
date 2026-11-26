cmake_minimum_required(VERSION 3.10)

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)

include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(portzone C CXX)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_ALTER "0")
set(PROJECT_AUTHOR "JohnUzoka")
# The homebrew menu needs a 256x256 JPEG; resources/icon/icon.png is the same logo for the app's header.
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

if (USE_LIBROMFS)
    add_libromfs(${PROJECT_NAME} ${PROJECT_RESOURCES})
endif ()

add_subdirectory(${BOREALIS_LIBRARY} borealis)

# For Ryujinx without the console's firmware: no system fonts (pl), so the text is Inter only and the button
# symbols in the bottom bar show as boxes. Never for release builds.
option(PORTZONE_EMULATOR_BUILD "Switch build for testing in Ryujinx without firmware" OFF)
if (PORTZONE_EMULATOR_BUILD)
    target_compile_definitions(borealis PRIVATE BOREALIS_NO_SHARED_FONTS)
endif ()

find_package(Threads REQUIRED)

file(GLOB_RECURSE APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(APPEND APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz/miniz.c)
if (PLATFORM_SWITCH)
    list(APPEND APP_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
endif ()

program_target(${PROJECT_NAME} "${APP_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PORTZONE_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}"
    MINIZ_NO_TIME)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniz
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/picosha2)
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections)
if (PORTZONE_EMULATOR_BUILD)
    # switch_wrapper.c is built here
    target_compile_definitions(${PROJECT_NAME} PRIVATE BOREALIS_NO_SHARED_FONTS PORTZONE_EMULATOR_BUILD)
endif ()

if (PLATFORM_SWITCH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
    target_link_libraries(${PROJECT_NAME} PRIVATE borealis PkgConfig::CURL Threads::Threads)
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        # No --titleid: homebrew does not need one, and Ryujinx refuses an .nro whose NACP has one it does not know.
        COMMAND ${NX_NACPTOOL_EXE} --create "PortZone" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        # The port list and every port's portzone.json as they were at build time: used while the store can't be reached.
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/store/ports.json ${CMAKE_CURRENT_SOURCE_DIR}/store/manifests.json ${CMAKE_BINARY_DIR}/resources/
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources)
else ()
    find_package(CURL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE borealis CURL::libcurl Threads::Threads)
    # On a PC the console's own fonts (text fallback, button symbols) come from borealis' copy; on the Switch the
    # system provides them, so they are not part of PortZone's resources.
    add_custom_target(${PROJECT_NAME}.data
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E copy ${BOREALIS_DIR}/resources/font/switch_font.ttf ${BOREALIS_DIR}/resources/font/switch_icons.ttf ${CMAKE_CURRENT_BINARY_DIR}/resources/font/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/store/ports.json ${CMAKE_CURRENT_SOURCE_DIR}/store/manifests.json ${CMAKE_CURRENT_BINARY_DIR}/resources/)
    if (NOT USE_LIBROMFS)
        add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
    endif ()
endif ()
