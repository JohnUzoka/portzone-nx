# Using PortZone

## Browse, Installed, Game files

**Browse** lists every port, as cards or a grid of icons. **Search** (or **−**) finds games by name, author or
system. **Filter and view** (or **X**) chooses the kind of port, the system the game came from (PC, 3DS, Xbox 360,
source code...), the order (name A-Z or Z-A, or by system) and cards or grid. **Installed** has the same search and
choices. The kinds:

- **Easy Setup**: the port has a `portzone.json`. One-button install, a checklist of your files, the folder layout.
- **Prepare on PC**: PortZone installs the game; a tool on a PC makes its files from your copy. PortZone links the
  tool, shows the steps, checks the result.
- **Build on PC**: a tool on a PC makes everything, the game included. Nothing to download in PortZone: it links the
  tool, takes what the tool made (upload page or SD card) and checks it.
- **Manual**: no `portzone.json`. Its README and Switch release files; you pick where a file goes.

**Installed** lists what is on the SD card. **Game files** gets your files into place. **About** has the theme, the
console, the language, updates and sources. **Y** reloads the port list.

**Console** (About): Switch or Switch 2. PortZone can't tell them apart, so set it once: games with a separate Switch
2 build install that build on a Switch 2, and games made for the other console are listed but not installed.

**Language**: PortZone follows the console's language (English, Spanish, French, German, Italian, Portuguese (Brazil),
Japanese, Simplified Chinese; any other shows English). **Language** in About overrides it from the next start. Game
titles and descriptions stay as their porters wrote them. The upload page follows the browser's language.

## Getting your files onto the SD card

PortZone matches what you offer against what each port needs and puts every file where its port wants it; you never
sort files yourself. From the Game files tab, or from a game's page for just that game:

- **Send from phone or PC**: while that screen is open, the Switch serves a page on your network
  (`http://<switch address>:8080/?k=<key>`). Phone or PC and Switch must be on the same network; no internet needed.
  Drop files or whole folders, several at once, even a folder of many games: the browser sends only names and sizes,
  PortZone answers with what it found and where it goes, and only those files are uploaded. A PC build's output (an
  `sdcard` folder, or its `switch` and `mono` folders together) goes to the card's root, each part where it belongs.
  Each is checked (size, and SHA-256 when the
  port lists one) before it's put in place. For a Build on PC game the page starts with the tool's link. The page's
  list of games can be searched and sorted too.
- **Find on SD card**: copied files somewhere on the card yourself (with a card reader, FTP, MTP)? PortZone searches
  the card (not the system's or CFW's folders, its own data, or the ports' folders) and moves what you pick into
  place. A folder that came over whole is removed once empty; the folder a single file came from stays.
- **Placing files yourself**: on the upload page every game has **Choose where files go myself**, a file browser that
  opens at the game's folder (or `switch/<repository name>`). Make folders and upload files or whole folders where
  you want. The system's and CFW's folders and PortZone's own data are off limits.

When two ports want the same file, or two files fit one game (two `.3ds` dumps), PortZone asks you. A single file over
4 GB needs an exFAT SD card.

## Updates

- **PortZone itself**: at start (unless turned off in About) it checks for a new version; Browse shows a banner, and
  **See update** downloads it, checks it and offers to restart.
- **Your games**: the same check looks at every game PortZone installed. One with a new release shows **Update: <tag>**
  on its card, the sidebar reads **Installed (N)**, and the game's page has **Update to**. Games installed outside
  PortZone aren't checked.
- **Test builds**: a game whose repository has prereleases gets a **Test builds** switch on its page. On, Install,
  Update and the check use the newest release, prereleases included.

## What PortZone does and doesn't touch

PortZone never downloads game data. Release downloads are checked against GitHub's SHA-256. Removing a port deletes
only the files PortZone installed (recorded in `sdmc:/config/portzone/installed/`); your game files and saves stay.
