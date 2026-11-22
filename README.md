# Big clock

A clock tile whose digits fill the tile. Tessera's own fonts stop at 64 px, and drawing them larger blurs them, so
this plugin brings the screen's own Roboto in six large sizes for the time (96 to 288 px, only the digits and the
colon) and two for the date (36 and 60 px), about 700 KB of flash, and the tile takes the largest that fits. It also
adds a **screensaver face** for the Clock step: the same clock on the whole black glass with a larger date under it
(Screen settings > Screensaver > Clock > Face: Big clock). It is meant for the larger boards with flash to spare.

- The time follows the screen's 12/24-hour setting; a 12-hour clock shows AM/PM beside the digits.
- Under it, the weekday and date: long ("Saturday, November 21"), short ("Sat Nov 21") or none. The long form falls
  back to the short one when the tile is too narrow.
- Optional seconds, smaller, beside the minutes.
- Colours follow the screen's theme, light and dark.
- No data, no network, no entity: the screen's own clock. Until Home Assistant has set the clock it says
  "Waiting for the time".

## Which screens

Made for the **larger touchscreens** (7" and 10" ESP32-P4 boards such as the Guition JC1060P470 and JC8012P4A1, which
have 16 MB of flash and plenty of PSRAM). The digits are drawn at full size, so they stay sharp at any tile size. Tessera's own fonts stop at 64 px and drawing them larger only blurs
them, so this plugin brings real large sizes of Roboto (the screen's own typeface). That costs about 700 KB of flash,
which the 4 MB boards (most 4" screens) do not have to spare. Install it only on screens that can carry the extra
overhead.

## Install for testing

Copy this folder to `/config/tessera-plugins/big_clock/` on Home Assistant. It then shows on Tessera's Plugins page
labelled Test. Add it to a screen, and place the **Big clock** tile from the library's Plugins group.
