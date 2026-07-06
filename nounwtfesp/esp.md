# ESP8266 MAX7219 Grant Display Setup

This guide explains how to prepare, wire, program, and test the ESP8266 + MAX7219 LED matrix display for [`esp8266.ino`](./esp8266.ino).

Current focus:

- ESP8266 dev board
- CH34x USB serial driver on Windows
- MAX7219 LED matrix display

This folder includes the Windows CH34x USB serial driver installer:

```text
CH34x_Install_Windows_v3_4.zip
```

Use that local driver ZIP when setting up the ESP8266 from this repository.

The sketch does this:

1. Shows `noun.wtf` one character at a time while connecting to WiFi.
2. Fetches grant data from `https://nounv2api.vercel.app/api/grant`.
3. Scrolls the grant ID, title, and proposer on the MAX7219 display.

Example output:

```text
#34 | NOUN.WTF://PHYSICALART | xppaicyber.eth
```

## 1. Parts

- ESP8266 dev board, such as NodeMCU or Wemos D1 mini
- MAX7219 8x8 LED matrix module, or chained MAX7219 modules
- Jumper wires
- USB data cable for the ESP8266
- Computer with Arduino IDE
- 5V power source or power bank
- Bundled CH34x Windows driver ZIP: [`CH34x_Install_Windows_v3_4.zip`](./CH34x_Install_Windows_v3_4.zip)

Use a real USB data cable. Many charging cables power the board but do not expose the serial port.

## 2. Prepare The Hardware

1. Identify the ESP8266 board type.
   - NodeMCU usually has pins labeled `D0` to `D8`.
   - Wemos D1 mini also uses `D0` to `D8`.
   - Bare ESP-12 modules use raw GPIO numbers and need extra boot wiring.
2. Use an ESP8266 board with a CH34x USB serial chip.
   - Most low-cost NodeMCU and Wemos-compatible ESP8266 boards use `CH340`, `CH341`, or another `CH34x` chip.
   - This repository includes the CH34x Windows driver ZIP for those boards.
3. Check the MAX7219 module direction.
   - The first module must be connected on its input side.
   - Input pins are usually labeled `DIN`, `CS` or `LOAD`, `CLK`, `VCC`, and `GND`.
   - Output pins are usually labeled `DOUT`, `CS`, `CLK`, `VCC`, and `GND`.
4. Count the number of 8x8 matrices.
   - One single 8x8 module = `1`
   - One common 32x8 module = `4`
   - Two 32x8 modules chained = `8`
5. Keep brightness low at first. MAX7219 modules can draw a lot of current.

## 3. Install CH34x USB Serial Driver

Do this first on Windows if the ESP8266 board does not appear as a COM port. This project is set up for CH34x-based ESP8266 dev boards.

The bundled driver file is:

```text
nounwtfesp\CH34x_Install_Windows_v3_4.zip
```

### Install On Windows

1. Open the `nounwtfesp` folder.
2. Find [`CH34x_Install_Windows_v3_4.zip`](./CH34x_Install_Windows_v3_4.zip).
3. Right-click the ZIP file.
4. Click `Extract All`.
5. Extract it into a normal folder.
6. Open the extracted folder.
7. Run `CH34x_Install_Windows_v3_4.EXE`.
8. If Windows asks for permission, click `Yes`.
9. Click `INSTALL`.
10. Wait for the installer to finish.
11. Unplug the ESP8266 if it is already connected.
12. Plug the ESP8266 back into USB.
13. Open `Device Manager`.
14. Open `Ports (COM & LPT)`.
15. Confirm that a new COM port appears.

The port may appear with a name like:

- `USB-SERIAL CH340 (COM3)`
- `USB-SERIAL CH340 (COM4)`
- `USB-SERIAL CH341 (COM7)`

The exact COM number depends on your computer. Use that COM port later in Arduino IDE under `Tools > Port`.

### Confirm The Driver Works

1. Open `Device Manager`.
2. Disconnect the ESP8266 USB cable.
3. Watch which COM port disappears.
4. Connect the ESP8266 USB cable again.
5. Watch which COM port appears again.
6. That is the ESP8266 upload port.

If no COM port appears:

1. Try a different USB cable.
2. Try a different USB port.
3. Avoid USB hubs during setup.
4. Re-run `CH34x_Install_Windows_v3_4.EXE`.
5. Restart Windows.
6. Connect the ESP8266 again.

## 4. Install Arduino IDE

1. Download and install Arduino IDE.
2. Open Arduino IDE.
3. Open `File > Preferences`.
4. Find `Additional boards manager URLs`.
5. Add this URL:

   ```text
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```

6. If there is already another URL in the field, separate URLs with commas.
7. Click `OK`.

## 5. Install ESP8266 Board Support

1. Open `Tools > Board > Boards Manager`.
2. Search for `esp8266`.
3. Install `esp8266 by ESP8266 Community`.
4. Wait until installation finishes.
5. Select your board:
   - NodeMCU: `Tools > Board > ESP8266 Boards > NodeMCU 1.0`
   - Wemos D1 mini: `Tools > Board > ESP8266 Boards > LOLIN(WEMOS) D1 R2 & mini`

## 6. Install Display Libraries

1. Open `Tools > Manage Libraries`.
2. Search for `MD_Parola`.
3. Install `MD_Parola by MajicDesigns`.
4. Search for `MD_MAX72XX`.
5. Install `MD_MAX72XX by MajicDesigns`.

The WiFi and HTTPS libraries used by the sketch are included with the ESP8266 board package.

## 7. Wire ESP8266 To MAX7219

Disconnect USB power before wiring.

| MAX7219 pin | NodeMCU / Wemos pin | ESP8266 GPIO | Purpose |
| --- | --- | --- | --- |
| `VCC` | `5V` / `VIN` | 5V rail | Powers MAX7219 module |
| `GND` | `GND` | Ground | Common ground |
| `DIN` | `D7` | `GPIO13` | SPI data |
| `CS` / `LOAD` | `D8` | `GPIO15` | Chip select |
| `CLK` | `D5` | `GPIO14` | SPI clock |

Important power notes:

- Do not connect MAX7219 `VCC` to ESP8266 `3V3`.
- Do not connect 5V directly to any ESP8266 GPIO pin.
- ESP8266 GPIO is 3.3V logic. Most MAX7219 modules accept it, but use short wires and common ground.
- For multiple matrix modules, use a separate 5V supply if USB power is unstable.
- The external 5V supply ground and ESP8266 ground must be connected together.

## 8. Wire Multiple MAX7219 Modules

Connect the ESP8266 to the input side of the first module. Then chain modules like this:

| First module | Next module |
| --- | --- |
| `DOUT` | `DIN` |
| `CLK` | `CLK` |
| `CS` / `LOAD` | `CS` / `LOAD` |
| `VCC` | `VCC` |
| `GND` | `GND` |

Then update this line in [`esp8266.ino`](./esp8266.ino):

```cpp
#define MAX_DEVICES 4
```

Use the number of individual 8x8 matrices:

- One single 8x8 module: `1`
- One 32x8 module with four matrices: `4`
- Two 32x8 modules chained together: `8`
- Two single 8x8 modules chained together: `2`

## 9. Configure WiFi And API

Open [`esp8266.ino`](./esp8266.ino) and edit:

```cpp
const char WIFI_SSID[] = "xxx";
const char WIFI_PASSWORD[] = "xxx";
```

Use your real WiFi name and password before uploading.

The API endpoint is:

```cpp
const char GRANT_API_URL[] = "https://nounv2api.vercel.app/api/grant";
```

Expected API response:

```json
{
  "success": true,
  "limit": 4,
  "data": [
    {
      "id": "34",
      "title": "NOUN.WTF://PHYSICALART",
      "proposer": "xppaicyber.eth"
    },
    {
      "id": "33",
      "title": "Raspberry Pie",
      "proposer": "fattybuthappy.eth"
    }
  ]
}
```

The sketch reads up to 4 grant objects from `data`:

- `data[].id`
- `data[].title`
- `data[].proposer`

Do not publish the sketch publicly while it contains a real WiFi password.

## 10. Upload The Sketch

1. Connect the ESP8266 to USB.
2. Open [`esp8266.ino`](./esp8266.ino) in Arduino IDE.
3. Select the board from `Tools > Board`.
4. Select the port from `Tools > Port`.
5. Use these common upload settings:
   - Upload Speed: `115200`
   - CPU Frequency: `80 MHz`
   - Flash Size: usually `4M`
6. Click `Verify` first.
7. If verification succeeds, click `Upload`.
8. Wait until Arduino IDE says upload is complete.

If upload fails, close Serial Monitor and try again.

## 11. Test The Display

1. Keep the ESP8266 connected by USB.
2. Open `Tools > Serial Monitor`.
3. Set baud rate to `115200`.
4. Reset the ESP8266.
5. Watch the MAX7219 display.

Expected boot flow:

1. While connecting to WiFi, the display shows `noun.wtf` one character at a time.
2. After WiFi connects, it briefly displays `WiFi connected`.
3. Then it displays `Loading grant...`.
4. The ESP8266 fetches the grant API.
5. The matrix scrolls each grant one by one, for example:

   ```text
   #34 | NOUN.WTF://PHYSICALART | xppaicyber.eth
   ```

The sketch fetches fresh API data every 5 minutes. If WiFi or API fetch fails, it retries every 30 seconds.

## 12. Manual Text Test

You can temporarily override the display text from Serial Monitor:

1. Open Serial Monitor at `115200`.
2. Type a message.
3. Press Enter.

The matrix scrolls your typed message. The next API refresh replaces it with the latest grant data.

## 13. Useful Sketch Settings

Brightness:

```cpp
const uint8_t BRIGHTNESS = 3;
```

Valid values are `0` to `15`. Start with a low value.

Scroll speed:

```cpp
const uint16_t SCROLL_SPEED_MS = 40;
```

Lower values scroll faster. Higher values scroll slower.

Pause after each scroll:

```cpp
const uint16_t SCROLL_PAUSE_MS = 1000;
```

Startup letter speed:

```cpp
const unsigned long STARTUP_LETTER_INTERVAL_MS = 350;
```

API refresh interval:

```cpp
const unsigned long API_REFRESH_INTERVAL_MS = 300000;
```

API retry interval:

```cpp
const unsigned long API_RETRY_INTERVAL_MS = 30000;
```

## 14. If The Text Looks Wrong

Most MAX7219 matrix modules use `FC16_HW`, which is selected in the sketch:

```cpp
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
```

If text is mirrored, upside down, shifted, or scrambled, try one of these:

```cpp
#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW
#define HARDWARE_TYPE MD_MAX72XX::GENERIC_HW
#define HARDWARE_TYPE MD_MAX72XX::ICSTATION_HW
```

Only leave one `HARDWARE_TYPE` line enabled at a time.

## 15. Troubleshooting

Board does not appear in Arduino IDE:

- Try another USB cable.
- Try another USB port.
- Check `Device Manager`.
- Extract and install [`CH34x_Install_Windows_v3_4.zip`](./CH34x_Install_Windows_v3_4.zip).
- Unplug and reconnect the board after installing the driver.
- Restart Windows if the COM port still does not appear.

Upload fails:

- Close Serial Monitor.
- Select the correct COM port.
- Select the correct ESP8266 board.
- Press and hold `FLASH` while upload starts if your board does not auto-enter bootloader mode.
- Release `FLASH` when Arduino IDE starts writing.
- Press `RST` once if the board does not run after upload.

No LEDs turn on:

- Check `VCC` and `GND`.
- Confirm the MAX7219 module is powered from 5V.
- Confirm the ESP8266 and MAX7219 share ground.
- Make sure wires are connected to the input side of the first MAX7219 module.

Random pixels or flickering:

- Reduce `BRIGHTNESS`.
- Use a stronger 5V supply.
- Keep wires short.
- Confirm common ground.
- Avoid powering many matrices only from the ESP8266 USB 5V pin.

Only part of the display works:

- Set `MAX_DEVICES` to the correct number of 8x8 matrices.
- Check the chain direction: `DOUT` from the previous module goes to `DIN` on the next module.
- Check for loose jumper wires.

ESP8266 does not boot:

- GPIO15 must be low during boot.
- If using `D8` / `GPIO15` for `CS` causes boot problems, move `CS` / `LOAD` to `D2` / `GPIO4`.
- Then update the sketch:

  ```cpp
  const uint8_t CS_PIN = 4;
  ```

Display shows `WiFi failed`:

- Check `WIFI_SSID`.
- Check `WIFI_PASSWORD`.
- Make sure the network is 2.4 GHz. ESP8266 does not support 5 GHz WiFi.
- Move the ESP8266 closer to the router.

Display shows `API request failed`:

- Open Serial Monitor at `115200`.
- Check the printed HTTP status code.
- Confirm the WiFi network has internet access.
- Open `https://nounv2api.vercel.app/api/grant` in a browser.

Display shows `API parse failed`:

- Open Serial Monitor and read the printed API response.
- The sketch expects `id`, `title`, and `proposer` string fields.

## 16. Reference Links

- Bundled CH34x Windows driver: [`CH34x_Install_Windows_v3_4.zip`](./CH34x_Install_Windows_v3_4.zip)
- ESP8266 Arduino core: https://github.com/esp8266/Arduino
- ESP8266 board manager URL: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
