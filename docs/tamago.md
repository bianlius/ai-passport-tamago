<p align="right"><a href="tamago.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Tama-Go on AI Passport

This application boots the user's 640 KiB Tama-Go ROM with the T7Emu CPU and
peripheral core. It provides a new Passport screen, button mapping, audio, and
NVS-backed EEPROM/session storage. The original hardware-test demo sources
remain as references but are not compiled into this application.

## Controls

| Passport button | Tama-Go button |
| --- | --- |
| UP | A |
| DOWN | B |
| OK | C |

Only the Passport's physical buttons control the game; no on-screen controls
are displayed. Press and release are forwarded to the emulated toy. The display
uses landscape orientation (320 × 240). Five 24 × 24 icons appear above the
game LCD and five below it; their brightness follows the Tama-Go display RAM.
Each native LCD pixel fills a 5 × 5 square. A subtle one-screen-pixel edge
keeps individual cells visible without breaking up the game's outlines.
The LCD uses a soft gray-white four-shade palette rather than pure white.
They use one consistent [Lucide Icons](https://lucide.dev/icons) set. In order,
the upper row means Information, Food, Toilet/Flush, Doors, and Gotchi Figure;
the lower row means Training, Medicine, IR Communication, Album, and Attention.
The meanings and layout follow the
[Bandai Tama-Go Quick Start Guide](https://manuals.plus/m/a3d3256736d3d52c1ec7eca6353801f04e9d84f6b2cfcaf6490c914fed6868f3).
The SVG and raster sources, license, and conversion method are recorded in
[`assets/images/tamago_lucide`](../assets/images/tamago_lucide).
The upper and lower icons are inactive during birthday and name setup and
while the egg hatches. Use UP (A) to select a value and DOWN (B) to confirm;
UP (A) cycles icons after the pet's main screen appears.

Hold physical UP for 1.5 seconds to open Settings. Release UP, use UP/DOWN to
choose Volume, Brightness, Gotchi Figures, or Return to Game,
then press OK.
Volume and brightness are displayed as percentages. In either adjustment view,
UP/DOWN changes the value by 5 percentage points, with a live preview; OK saves
it to NVS and returns to the Settings list. Volume ranges from 0 to 100%;
brightness ranges from 5 to 100% so the screen remains usable.

The Gotchi Figures view shows only imported figures, plus None, File Transfer, and Return to
Settings. Use UP/DOWN to choose an imported figure, then briefly press OK to
select it and return to Settings. Hold OK for about 1.5 seconds to return at
any time without changing figures. Briefly pressing OK on File Transfer opens
the wireless/USB choice screen; Return to Settings leaves the current figure unchanged.
With no imported figures, None, File Transfer, and Return to Settings remain available. The old figure is removed immediately; a
new one is inserted after 2.5 seconds so the Tama-Go game can detect the change.
The selection is saved in NVS and restored at startup. The menu uses text only;
it adds no virtual buttons to the game screen. Its title, instructions, and
None option are in Simplified Chinese. Each figure displays a Chinese UI name
followed by its original English name. The Chinese names are interface
translations, not claimed official localizations.

## Importing ROMs

The simulator firmware contains no game or figure ROM. On first boot without
a game ROM, the choice screen explains that no game ROM was detected and asks
the user to choose a transfer method. Use UP/DOWN to choose wireless or
USB transfer, then press OK. If a game ROM is already available, Start Game is
also offered. The hotspot starts and its Wi-Fi QR code appears only after
choosing wireless. Scan it with a phone to join the device
hotspot. The phone should offer the local transfer page. If it does not, press
UP on the device to show a second QR code for the web address and scan it, or
open `http://192.168.4.1/` in the phone browser while connected to the hotspot.
The wireless page uses ordinary file selection and HTTP transfer in common
phone or desktop browsers. If the operating system's automatic captive portal
window cannot select ROM files or download saves, keep the device hotspot
connected and open that address in a full browser such as Safari or Chrome.
Upload your own 655,360-byte Tama-Go game dump, then select one or several
figure files in the single Figure ROM input. The page reads each ROM's internal
figure ID and assigns its slot automatically. Unknown files and duplicate
figures in one selection are rejected before upload. The first six figure files
must each be 349,536 bytes; the last six must each be 524,288 bytes. The device
also checks the internal ID, size, and Flash contents before making a figure
available. The page shows one figure upload area and a summary of imported figures.
Choose “Finish and start game” after uploading the game ROM. Hold UP during
gameplay, open Gotchi Figures, and choose File Transfer to return to the transfer choice screen later.
This saves the current pet session before restarting the application. On the
Wi-Fi screen, DOWN returns to the choice screen; on the USB screen, UP does the
same. Neither transfer service starts before a mode is chosen.

For wired transfer, connect the Passport to a computer with a data-capable USB
cable and select USB Transfer on the device. Copy the single-file
[`tools/TamaGo-USB.html`](../tools/TamaGo-USB.html) to that computer and open it
in a browser that supports [Web Serial](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API),
preferably desktop Chrome or Edge. Click Connect Device and select the connected
AI Passport in the browser's serial-port picker. Port names and USB IDs can vary
between devices; the page verifies the Tama-Go transfer protocol after selection.
The local page transfers
ROMs and saves directly over USB Serial; it needs no Python, dependencies,
local server, or Internet connection. Desktop Chrome/Edge on Windows, macOS,
and Linux can use this flow, subject to browser and OS serial permissions.
For other browsers, use the page's Web Serial capability check; phones and
tablets should generally use the device-hosted wireless page. Wait for each
Import Successful message before unplugging, then click Finish and Start Game.
After importing the main game, OK on the device can also start it.

The older `tools/tamago_usb_transfer.py` bridge and Mac
`tools/TamaGo-USB.command` launcher remain as fallbacks. The bridge requires
Python and `pyserial` and serves a local page at `http://127.0.0.1:8765/`.
It cannot use the same USB port at the same time as the direct browser page.
The older bridge auto-discovers only the known USB ID; for another ID, run
`python3 tools/tamago_usb_transfer.py --port PORT` with its serial port.

## Save backup and restore

Open Gotchi Figures in Settings and choose File Transfer, then use either the phone Wi-Fi page or the
computer USB page. Choose **Export Save** to download a `.tgsave` file. It
contains the 4 KiB emulated EEPROM and, when available, the latest session
snapshot. The file does not contain game/figure ROMs, Wi-Fi credentials,
volume or brightness settings.

To restore, import the matching main game ROM first, select the `.tgsave` file,
and choose **Import Save**. The page confirms that the current pet save will be
replaced. The device checks file format, CRC, and main ROM identity before
writing. Keep the device powered during import, then choose “Finish and start
game.” Export the current save before replacing it so it can be restored if a
write is interrupted. The backup stays on the phone or computer that downloaded
it; the device does not upload it to a cloud service.

The application occupies a 2 MiB partition and the imported ROMs occupy a
separate data partition starting at `0x210000`. The NVS partition containing
pet saves remains at `0x9000`. Application-only updates can preserve both data
partitions when flashed with the same layout; a full-chip erase or incompatible
partition change can remove them. The 12 figure files are optional. ROMs remain
private to the owner's device and are excluded from Git and firmware images.

Use ESP-IDF 5.5.3 and run `./tools/validate.sh`. Developers with a local game
dump may run the optional host smoke test `./tools/test_tamago_core.sh`.

The imported `main/t7_core` comes from Paramix Studio's T7Emu and is covered
by its GPL-3.0-or-later license and upstream notices in that directory. The
upstream project credits asterick's BSD-licensed `tamago` as the original
emulator. No ROM should be committed or published with the source.

## Persistence and current limits

The emulated 4 KiB EEPROM is stored in NVS, together with a session snapshot
every minute. A sudden power loss may lose up to one minute of in-memory
progress. The session is tied to the imported game ROM hash, so a different ROM
will start a new session. Snapshot version 2 stores the 6502's live status
flags. Older snapshots can still be loaded; interrupts are re-enabled on
migration so the game can resume.
The Passport has no confirmed battery-backed clock
source in this project; pet time pauses while the device is fully powered off.
Not every sound effect and figure combination has been checked on-device.
On the tested Passport, four 200-frame windows in a restored pet scene averaged
49.95–50.00 ms per complete 50 ms game frame after the delay-loop and frame
scheduling optimizations. During button and menu activity, another four windows
averaged 50.00 ms. The owner confirmed smoother animation, working buttons, and
normal audio.
