# 3DS Ceefax

A live teletext client for the Nintendo 3DS. It reads the
[NMS Ceefax](https://www.nmsni.co.uk/) service directly from its public feeds.

- **Top screen:** the 40×25 teletext page in the SAA5050 character set, drawn
  pixel-for-pixel with no scaling or filtering.
- **3D:** with the 3D slider up, the page floats in front of the black background.
- **Bottom screen:** a remote control with a keypad, page and subpage stepping, hold,
  reveal, region selection, and fastext keys.
- **Live updates:** the service is polled every 30 seconds and changed pages refresh
  on screen.
- **Level 1 teletext:** colours, contiguous and separated mosaics, held mosaics, double
  height, flash, conceal/reveal and carousels.
- **Regions:** all NMS Ceefax regions.

## Controls

| Input | Action |
|---|---|
| Touch keypad | Enter a page number |
| D-Pad ↑/↓ | Next / previous page |
| D-Pad ←/→ | Previous / next subpage |
| R | Next page |
| L | Bottom screen on/off (tap the screen to wake it) |
| A | Reveal |
| X | Hold |
| Y | Index (P100) |
| B | Back |
| SELECT | Next region |
| START | Quit |

Settings are saved to `sdmc:/3ds/ceefax.cfg`. If your 3DS clock is badly wrong and
HTTPS fails, add `verify_tls=0` to that file.

## Building

Requires [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS
toolchain and curl:

```sh
dkp-pacman -S 3ds-dev 3ds-curl
make
```

The output is `ceefax.3dsx`. Copy it to `sdmc:/3ds/` on the SD card.

### CIA

You also need [makerom](https://github.com/3DSGuy/Project_CTR/releases) and
[bannertool](https://github.com/diasurgical/bannertool), either on your `PATH` or in
`tools/bin/`:

```sh
make cia
```

### Desktop tests

The parser, renderer, fetcher and app logic also build natively against libcurl. The
test harnesses use the live feed and write screenshots as BMPs:

```sh
make -C test && ./test/app_test /tmp/shots
```

### Regenerating assets

```sh
curl -O https://bjh21.me.uk/bedstead/bedstead.c
python3 tools/gen_font.py bedstead.c > source/font.h
python3 tools/gen_assets.py source/font.h
curl -o romfs/cacert.pem https://curl.se/ca/cacert.pem
```

## Credits

- Teletext pages come from [NMS Ceefax](https://www.nmsni.co.uk/) by Nathan Media
  Services. This project is not affiliated with NMS or the BBC.
- The page format and feed locations are from
  [vbit2](https://github.com/peterkvt80/vbit2) by Peter Kwan and contributors.
- The character set is from [Bedstead](https://bjh21.me.uk/bedstead/) by Ben Harris,
  Simon Tatham, Marnanel Thurman and Neil Williamson (CC0). It reproduces the Mullard
  SAA5050.
- The app is built with [devkitPro](https://devkitpro.org/) and
  [libctru](https://github.com/devkitPro/libctru), and statically links
  [libcurl](https://curl.se/), [Mbed TLS](https://www.trustedfirmware.org/projects/mbed-tls/)
  and [zlib](https://zlib.net/).
- `romfs/cacert.pem` is the Mozilla CA bundle, as distributed by the curl project
  (MPL 2.0).