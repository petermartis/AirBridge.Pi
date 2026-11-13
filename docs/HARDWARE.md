# Hardware and wiring
## Current prototype
CM5 Lite 4 GB, Terioto BentoIO CMX2 carrier, official 27 W supply, SD card, Mercusys MA86XH and a red 2.8-inch SPI TFT module marked `240xRGBx320 V1.1`. The carrier exposes a standard Raspberry Pi-compatible **40-pin header**. These are not the CM5 module's board-to-board connector pin numbers.

Disconnect power before changing wiring. GPIO signals are **3.3 V only**. Confirm header pin 1/orientation on the carrier; a mirrored connector view is not a different GPIO numbering scheme.

## Display: working assignment
| LCD label | BCM signal | Physical header pin |
|---|---|---:|
| VCC | 3.3 V supply, as originally instructed | 1 |
| GND | Ground | 6 |
| CS | GPIO8 / SPI0 CE0 | 24 |
| RESET | GPIO25 | 22 |
| DC / RS | GPIO24 | 18 |
| SDI / MOSI | GPIO10 | 19 |
| SCK | GPIO11 | 23 |
| SDO / MISO | GPIO9 | 21 |
| LED | 3.3 V backlight connection, as instructed | 17 |

Backlight was initially disconnected and the screen dark. The user later corrected the hardware and reported a backlit white screen, then a working image. **No multimeter readings or exact final power-wiring photograph were recorded.** The table records the intended wiring, not a newly measured voltage. Do not infer 5 V compatibility from resolution, PCB color or a presumed regulator; verify the actual module/regulator before changing VCC. Do not power the backlight from a normal GPIO output.

The LCD board's separate `SD_SCK/SD_MISO/SD_MOSI/SD_CS` pads belong to its card socket, not the TFT/touch interface; they are unused.

## Touch: proposed, not enabled
The board has separate touch pins and likely an XPT2046/ADS7846-compatible resistive controller; its package identity/readings have not been verified. No touch handling or calibration exists yet.

| Touch label | BCM signal | Physical header pin |
|---|---|---:|
| T_CLK | GPIO11, shared SCK | 23 |
| T_DIN | GPIO10, shared MOSI | 19 |
| T_DO | GPIO9, shared MISO | 21 |
| T_CS | GPIO7 / SPI0 CE1, separate from TFT CS | 26 |
| T_IRQ | GPIO17, touch interrupt | 11 |

Only CE0 is currently enabled by `spi0-1cs`. A future touch step must back up boot configuration, enable a compatible CE1 path, verify GPIO17 is free, coordinate reboot if needed, then validate raw pressure/coordinates before mapping them to this panel's rotation/viewport. Do not blindly stack conflicting SPI overlays or bind both spidev and a kernel touch driver to the same CE1 device.

Test that the display remains stable while touch is sampled. If the display's SDO does not tri-state when deselected, shared MISO needs hardware investigation; do not diagnose the touch controller as dead solely from all-zero reads.

## Display initialization evidence
ILI9341-compatible commands produced a user-confirmed image. Read ID `0xD3` returned zeros and is inconclusive. The safe baseline is mode 0, 8-bit words, 1 MHz, RGB565 and a fresh column/page address window for each row. Rotation 90 plus explicit 320×240 is the actual verified viewport. The remaining controller-identification uncertainty should stay documented rather than guessed away.

## Ethernet daughterboard and storage
An earlier carrier/daughterboard cable issue was resolved. The recorded Terioto guidance was carrier-side LAN 12-pin and HDMI 20-pin contacts facing away from the PCB; check the specific connector's contacts and vendor documentation before reconnecting. This is not a universal FPC orientation rule.

SD remains the only validated boot medium. The SSD was not proved healthy; see [status](STATUS.md). Do not reuse its PCI function to reset the RP1 bus: RP1 also hosts critical Ethernet/USB/GPIO.

## References
- [CMX2 vendor listing and standard-header specification](https://www.lectronz.com/products/cmx2-low-profile-io-carrier-board-rpi-cm5-heatsink)
- [Raspberry Pi GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio)
