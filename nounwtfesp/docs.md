# NOUN.WTF ESP8266 MAX7219 Docs

This folder contains ESP8266 firmware and tools for displaying NOUN.WTF data on a 32x8 MAX7219 LED matrix.

## Files

| Path | Purpose |
| --- | --- |
| [`README.md`](./README.md) | Full setup tutorial for CH34x driver, wiring, flashing, Arduino IDE, and troubleshooting. |
| [`esp8266.ino`](./esp8266.ino) | Direct grant display sketch with WiFi credentials edited in code. |
| [`nounwtfgrant/nounwtfgrant.ino`](./nounwtfgrant/nounwtfgrant.ino) | Grant display firmware with ESP8266 WiFi setup portal. |
| [`nounwtfgrant/nounwtfgrant.bin`](./nounwtfgrant/nounwtfgrant.bin) | Ready-to-flash grant firmware binary. |
| [`nounwtfgrant/wifi_animation.h`](./nounwtfgrant/wifi_animation.h) | Custom WiFi connecting animation used by the grant firmware. |
| [`nounuwtfauction/nounuwtfauction.ino`](./nounuwtfauction/nounuwtfauction.ino) | Auction display firmware with ESP8266 WiFi setup portal. |
| [`nounuwtfauction/nounuwtfauction.bin`](./nounuwtfauction/nounuwtfauction.bin) | Ready-to-flash auction firmware binary. |
| [`max7219-animator/index.html`](./max7219-animator/index.html) | Browser-based 32x8 MAX7219 animation editor. |
| [`CH34x_Install_Windows_v3_4.zip`](./CH34x_Install_Windows_v3_4.zip) | Windows CH34x USB serial driver installer. |

## Hardware

- ESP8266 dev board, such as NodeMCU or Wemos D1 mini
- 32x8 MAX7219 matrix, usually four chained 8x8 modules
- USB data cable
- Jumper wires
- 5V power source

Default wiring:

| MAX7219 | ESP8266 NodeMCU / Wemos | GPIO |
| --- | --- | --- |
| `VCC` | `5V` / `VIN` | 5V rail |
| `GND` | `GND` | Ground |
| `DIN` | `D7` | `GPIO13` |
| `CS` / `LOAD` | `D8` | `GPIO15` |
| `CLK` | `D5` | `GPIO14` |

Use the input side of the MAX7219 module. The pins are usually labeled `DIN`, `CS`, `CLK`, `VCC`, and `GND`.

## Flash Ready-Made Firmware

Use the browser flasher:

```text
https://esptool.spacehuhn.com/
```

Flash one binary at address:

```text
0x00000
```

Grant firmware:

```text
nounwtfesp\nounwtfgrant\nounwtfgrant.bin
```

Auction firmware:

```text
nounwtfesp\nounuwtfauction\nounuwtfauction.bin
```

After flashing, reset the ESP8266.

## WiFi Setup

The portal firmware opens this access point when no saved WiFi works:

```text
NOUNWTF-SETUP
```

Connect your phone or computer to that network, then open:

```text
http://192.168.4.1
```

Save a 2.4GHz WiFi SSID and password. The ESP8266 restarts and connects automatically next boot.

## Grant Display

The grant firmware fetches:

```text
https://nounv2api.vercel.app/api/grant
```

It displays up to four grants from the API response:

- Grant ID
- Title
- Proposer

While connecting to saved WiFi, it plays the custom MAX7219 animation from:

```text
nounwtfesp\nounwtfgrant\wifi_animation.h
```

## Auction Display

The auction firmware fetches:

```text
https://nounv2api.vercel.app/api/auction
```

It displays:

- Noun ID
- Current bid
- Time left as a fitted `HH:MM:SS` display
- Bidder

The time-left screen is held longer than scrolling messages so it is easier to read.

## MAX7219 Animation Editor

Open this file directly in Chrome, Edge, or another modern browser:

```text
nounwtfesp\max7219-animator\index.html
```

Use it to draw and animate a 32x8 MAX7219 screen.

Workflow:

1. Draw or edit frames in the grid.
2. Use `Play` to preview the animation.
3. Adjust frame delay if needed.
4. Click `Copy Header` or `Download`.
5. Replace [`nounwtfgrant/wifi_animation.h`](./nounwtfgrant/wifi_animation.h) with the exported header.
6. Rebuild the grant firmware.
7. Flash the new `.bin` to the ESP8266.

The exported header is intentionally small. Each animation frame stores eight 32-bit row masks, so it fits comfortably on the ESP8266.

## Build From Arduino CLI

Grant firmware:

```powershell
& 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp8266:esp8266:nodemcuv2 --export-binaries --output-dir .\nounwtfesp\nounwtfgrant\build .\nounwtfesp\nounwtfgrant
```

Auction firmware:

```powershell
& 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp8266:esp8266:nodemcuv2 --export-binaries --output-dir .\nounwtfesp\nounuwtfauction\build .\nounwtfesp\nounuwtfauction
```

After exporting, copy the generated `.ino.bin` file to the final `.bin` path used by this repo.

## Notes

- Use 2.4GHz WiFi. ESP8266 does not support 5GHz WiFi.
- Keep MAX7219 brightness low at first.
- Do not power the MAX7219 from the ESP8266 `3V3` pin.
- Do not publish firmware source with real WiFi credentials hardcoded.
- If the ESP8266 reconnects to an old WiFi after flashing, clear saved WiFi from the setup page or erase flash before programming.
