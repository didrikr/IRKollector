# IRKollector

A small BLE peripheral that collects the Identity Resolving Key (IRK) of any phone that pairs with it. 

It advertises as `IRKollector-XXXX` (the last two bytes of its address) and looks like a keyboard, so it shows up in the normal Bluetooth menu on Android. Tap it, accept the pairing request, done. Bonds are kept in flash, so the keys survive resets.

Built with nRF Connect SDK v3.3.0. Tested on the nRF52832 DK, nRF52840 DK, nRF52840 dongle and nRF7002 DK. The nRF5340 DK builds but hasn't been run, since I didn't have one (the nRF7002 DK uses the same chip).

## Reading the IRK

Over serial (115200 baud, 8N1): a shell prints the IRK every time a phone pairs, and has a few commands.

```
irk list     show all stored IRKs, with the phone's name, manufacturer and address type
irk clear    forget all bonds
irk own      show the board's own address
```

The shell is on the DK's first USB serial port, except on the nRF7002 DK, where it is the second. On the dongle it is a USB serial port that appears after flashing.

Over the air: after pairing, open nRF Connect on the phone, connect to the board and read the IRK characteristic in the `49524b30-436f-6c6c-6563-746f72000001` service. It only returns the IRK of the phone that is asking. Characteristic `...02` is hex text, `...03` is the raw 16 bytes.

Each IRK is printed in both byte orders, because tools disagree on which one they want. MSB first is the one most people mean.

LED1 pulses while the board is waiting for a phone, is solid when connected, and blinks quickly when an IRK was captured. Hold the BUTTON1 for 3 seconds to clear all bonds.

## Building

The app is built outside the NCS workspace, so `west` is run from the SDK directory:

E.g. on macOS:

```
nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 --chdir /opt/nordic/ncs/v3.3.0 -- \
  west build -p -b <board> -d $PWD/build/<name> $PWD
```

Boards: `nrf52dk/nrf52832`, `nrf52840dk/nrf52840`, `nrf52840dongle/nrf52840`, `nrf5340dk/nrf5340/cpuapp`, `nrf7002dk/nrf5340/cpuapp`.

Flash the DKs with `west flash --no-rebuild -d build/<name> --dev-id <serial>`.

The dongle goes through its USB bootloader instead. Make a package with `nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req=0x00 --application <build>/IRKollector/zephyr/zephyr.hex --application-version 1 <out>.zip` (needs `nrfutil install nrf5sdk-tools`), press the reset button until the red LED pulses, then `nrfutil device program --firmware <out>.zip --serial-number <sn> --traits nordicDfu`.

## iPhone notes

iOS caches device names by address, so a board that ran other firmware before can show up under its old name until the first connection. nRF Connect shows the real name.
