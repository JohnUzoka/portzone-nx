#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "catalog.hpp"

// Sending game files from a phone or PC: while the Send files screen is open, PortZone serves a web page on the
// local network. Automatic: the browser lists what the player drops (names and sizes only), PortZone's matcher decides
// what goes where, and the browser uploads only those files, one request each. Manual: the player browses the SD card
// and uploads files into the folder they choose.
//
// HTTP API. Every /api/ request needs ?k=<key>, the random key in the address PortZone shows (403 otherwise).
//   GET  /?k=KEY                 the page (resources/web/index.html)
//   GET  /api/ports?k=KEY        {"focus": "<port id or empty>",
//                                 "ports": [{"id", "title", "author", "description", "icon" (https URL),
//                                            "platform" ("Xbox", "3DS", "PC", "Source" or ""),
//                                            "kind": "easy" | "pc" | "manual" (Easy Setup, Build on PC, Manual),
//                                            "installed", "status" ("Ready to play", "Installed · files missing"...),
//                                            "good" (status means all set), "dir" ("switch/oot3dre"),
//                                            "manualOnly": true when the port has no portzone.json game data,
//                                            "prepare": {"where", "summary", "tool": {"name", "url"} | null} | null,
//                                            "inputs": [{"id", "label", "required", "met",
//                                                        "options": [{"label", "files": [{"path", "note", "match",
//                                                                                          "markers": [..]}]}]}]}]}
//        Every port is listed. `dir` is where the manual browser opens for it: game_data.dir, else the folder its
//        release was installed to, else its .nro's folder, else switch/<repository name>. manualOnly ports have no
//        inputs.
//   POST /api/plan?k=KEY&port=ID&input=ID      port/input empty or absent: all ports / all inputs
//        body {"files": [{"path": "Half-Life/valve/liblist.gam", "size": 123}, ...]}
//        200 {"plan": N, "unused": count,
//             "matches": [{"id", "port", "portTitle", "input", "inputLabel", "option", "required", "alreadyMet",
//                          "group", "selected", "bytes", "files": [{"source": index into the posted files,
//                                                                   "dest": "valve/liblist.gam", "size"}]}]}
//        A new plan replaces the previous one.
//   PUT  /api/file?k=KEY&plan=N&match=I&file=J  body: the file's bytes; Content-Length must be the planned size.
//        200 {"ok": true} | 400/409/410 {"error": "..."} (409: wrong size or hash, 410: plan replaced)
//   POST /api/done?k=KEY&plan=N  the browser has sent everything it was going to; 200 {"ok": true}
//
// Manual placement. PATH is relative to the SD card's root, '/' separated, "" for the root. Refused (403): "..",
// "\", ":", the system's and CFW's folders (atmosphere/, Nintendo/, ...) and PortZone's own config/portzone/.
//   GET  /api/list?k=KEY&path=PATH     {"path", "exists", "folders": [name...], "files": [{"name", "size"}...]}
//                                      sorted by name; system folders are left out of the root's list
//   POST /api/mkdir?k=KEY&path=PATH    makes the folder (and those above it); 200 {"ok": true}
//   PUT  /api/put?k=KEY&path=PATH      body: the file's bytes (Content-Length required). Makes the folders, replaces
//                                      a file already there; 200 {"ok": true} | 4xx/500 {"error": "..."}
namespace pz::transfer
{

// What the Send files screen shows. A copy, safe to read on the UI thread.
struct Status
{
    bool running = false;
    std::string error;   // why it is not running: no network, no free port
    std::string address; // "http://192.168.1.23:8080/?k=4F7Q2Z"
    std::string phase;   // "Waiting for a browser", "Receiving halo.iso", "Done: 3 files"
    std::string currentFile;
    uint64_t currentDone = 0, currentTotal = 0;
    size_t filesDone = 0, filesTotal = 0; // in the current plan's uploads
    uint64_t bytesDone = 0, bytesTotal = 0;
    double bytesPerSecond = 0;
    std::vector<std::string> log; // finished files and errors, newest last
    unsigned generation      = 0; // changes whenever anything above does
};

// Starts serving for `focusPort` ("" = every port). `ports`: a copy of the catalog's ports, taken on the UI thread.
// Replaces a server already running. Returns false (see status().error) when it cannot.
bool start(std::vector<Port> ports, std::string focusPort);

// Stops serving; an upload in progress is abandoned and its partial file deleted.
void stop();

Status status();

} // namespace pz::transfer
