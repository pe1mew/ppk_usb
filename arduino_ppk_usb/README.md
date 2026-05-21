# arduino_ppk_usb — PlatformIO project

PlatformIO firmware for the Palm Portable Keyboard USB adapter, targeting the
SparkFun Pro Micro (ATmega32U4, 5 V / 16 MHz).

## Building and flashing

```
pio run --target upload
```

## Customising the USB device name and PID

By default the SparkFun Pro Micro enumerates as **VID 0x1B4F / PID 0x9206**
with the product string "SparkFun Pro Micro". Windows caches device names by
VID+PID, so if you plug the board in before customising it Windows will keep
showing the cached name even after you change the firmware strings.

The correct way to override both the product string **and** the PID in
PlatformIO with the Arduino AVR framework is a **custom board JSON**, not
`build_flags`. The reason:

- PlatformIO reads `USB_VID` and `USB_PID` from `build.hwids[0]` in the board
  JSON and injects them as `CPPDEFINES` (early in the compiler command).
- A `-DUSB_PID=…` entry in `build_flags` ends up in `CCFLAGS`, which appears
  *before* `CPPDEFINES` in the final command, so the board's value wins.
- Additionally, `USBCore.cpp` in the Arduino AVR core unconditionally resets
  `USB_MANUFACTURER` to `"SparkFun"` whenever `USB_VID == 0x1b4f`, so the
  manufacturer string cannot be overridden from `build_flags`.

### Step 1 — create `boards/ppk_usb.json`

Create a `boards/` directory next to `platformio.ini` and add a board
definition derived from `sparkfun_promicro16`. Change `hwids` to a PID that
has not been used on this Windows machine before, and set `usb_product` to the
string you want Windows to display.

```json
{
  "build": {
    "core": "arduino",
    "extra_flags": "-DARDUINO_AVR_PROMICRO16",
    "f_cpu": "16000000L",
    "hwids": [
      ["0x1B4F", "0xABCD"]
    ],
    "mcu": "atmega32u4",
    "usb_product": "Palm Portable Keyboard",
    "variant": "sparkfun_promicro"
  },
  "debug": { "simavr_target": "atmega32u4" },
  "frameworks": ["arduino"],
  "name": "PPK USB (SparkFun Pro Micro 5V/16MHz)",
  "upload": {
    "disable_flushing": true,
    "maximum_ram_size": 2560,
    "maximum_size": 28672,
    "protocol": "avr109",
    "require_upload_port": true,
    "speed": 57600,
    "use_1200bps_touch": true,
    "wait_for_upload_port": true
  },
  "url": "https://www.sparkfun.com/products/12640",
  "vendor": "SparkFun"
}
```

PlatformIO searches the project-local `boards/` directory before its own
platform boards, so no global installation is needed.

### Step 2 — point `platformio.ini` at the custom board

```ini
[env:promicro16]
platform   = atmelavr
board      = ppk_usb
framework  = arduino
lib_deps   = arduino-libraries/Keyboard
```

No `build_flags` overrides for USB strings are needed; everything comes from
the board JSON.

### Step 3 — flash and clear the Windows cache

Build and flash:

```
pio run --target upload
```

Unplug and replug the device. Because the PID changed, Windows treats it as a
new device and reads the string descriptors fresh from the hardware.

### Verifying the product string

Windows Device Manager shows class-based names ("USB Input Device",
"HID Keyboard Device") rather than the USB product string for composite
HID+CDC devices. To confirm the string reached Windows, run in PowerShell:

```powershell
Get-PnpDevice | Where-Object { $_.InstanceId -like "*VID_1B4F*PID_ABCD*" -and $_.InstanceId -notlike "*&MI_*" } |
    Get-PnpDeviceProperty -KeyName "DEVPKEY_Device_BusReportedDeviceDesc" |
    Select-Object Data
```

Expected output:

```
Data
----
Palm Portable Keyboard
```

You can also see it in Device Manager: right-click the device →
Properties → Details → **Bus reported device description**.

### Why the manufacturer stays "SparkFun"

`USBCore.cpp` in the Arduino AVR framework contains:

```cpp
#elif USB_VID == 0x1b4f
#  if defined(USB_MANUFACTURER)
#    undef USB_MANUFACTURER
#  endif
#  define USB_MANUFACTURER "SparkFun"
```

This runs at compile time and cannot be overridden without patching the
framework. Since the hardware genuinely is a SparkFun Pro Micro this is
accurate and acceptable. The product string ("Palm Portable Keyboard") is
what matters for identification.
