# esphome-usb-drivers

## Purpose
USB device drivers on the reworked `usb_host`: drives and optical drives as storages, keyboards / mice / gamepads /
remotes, UVC cameras, Bluetooth dongles, sound cards, serial and Ethernet adapters - each takes its devices by class.

## How it works
`usb_host` (esphome-usb-host) enumerates the devices, also behind hubs; every driver claims the devices of its class,
any number at once (`vid` / `pid` pin one device to one entry). Drives register with the storage interface
(`/usb`, `/cdrom`), HID devices feed the `hid` component, cameras become `camera` entities, sound cards speakers /
microphones, serial adapters `uart` buses, Ethernet adapters a network interface, Bluetooth dongles an HCI controller.

## Quick start
Chips: ESP32-S2, ESP32-S3, ESP32-P4, ESP32-S31

Needs: [esphome-usb-host](https://github.com/p1ngb4ck/esphome-usb-host),
[esphome-hid](https://github.com/p1ngb4ck/esphome-hid),
[esphome-storage-interface](https://github.com/p1ngb4ck/esphome-storage-interface),
[esphome-esp32-platform](https://github.com/p1ngb4ck/esphome-esp32-platform)
```yaml
external_components:
  - source: github://p1ngb4ck/esphome-usb-drivers@main
    components: [usb_audio, usb_bluetooth, usb_cdrom, usb_ethernet, usb_hid, usb_storage, usb_uart, usb_uvc]
  - {source: github://p1ngb4ck/esphome-usb-host@main, components: [usb_host]}
  - {source: github://p1ngb4ck/esphome-hid@main, components: [hid]}
  - {source: github://p1ngb4ck/esphome-storage-interface@main, components: [storage]}
  - {source: github://p1ngb4ck/esphome-esp32-platform@main, components: [esp32]}
usb_storage: [{devices: [{mount_path: /usb}]}]
usb_hid: [{id: keyboard}]
```

A USB stick at `/usb` and a keyboard; `usb_host`, `storage`, `hid`, `esp32` from their owners only. Also needed:
`usb_uart` uart-tcp, `usb_uvc` esp-video, `usb_audio` audio, `usb_ethernet` network, `usb_bluetooth` bluetooth.

## Main options
| Option | What it does | Default |
|---|---|---|
| [`usb_storage: devices`](CONFIGURATION.md#usb_storage) | drives with their own `mount_path` | `/usb`, `/usb2` ... |
| [`usb_cdrom: mount_path`](CONFIGURATION.md#usb_cdrom) | mount point of the first optical drive | `/cdrom` |
| [`usb_hid: layout`, `on_text`](CONFIGURATION.md#usb_hid) | `hid` input options (esphome-hid) | - |
| [`usb_uvc: width`, `height`](CONFIGURATION.md#usb_uvc) | requested resolution | camera's best |
| [`usb_uvc: camera`](CONFIGURATION.md#usb_uvc) | the camera entity, encoder `jpeg` or `h264` (P4) | `jpeg` |
| [`usb_bluetooth: bluedroid`](CONFIGURATION.md#usb_bluetooth) | Bluedroid host on the dongle | `false` |
| [`usb_audio: speaker`](CONFIGURATION.md#usb_audio) | wanted playback format | 48 kHz, 16 bit, 2 ch |
| [`usb_uart: devices`](CONFIGURATION.md#usb_uart) | adapter `type` and its `channels` (UART IDs) | CDC ACM |
| [`usb_ethernet: manual_ip`](CONFIGURATION.md#usb_ethernet) | static address | DHCP |

## Catalog
- Components (folder on main, configuration):
  - `usb_storage`: [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_storage),
    [configuration](CONFIGURATION.md#usb_storage)
  - `usb_cdrom`: [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_cdrom),
    [configuration](CONFIGURATION.md#usb_cdrom)
  - `usb_hid`: [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_hid),
    [configuration](CONFIGURATION.md#usb_hid)
  - `usb_uvc`: [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_uvc),
    [configuration](CONFIGURATION.md#usb_uvc)
  - `usb_bluetooth`:
    [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_bluetooth),
    [configuration](CONFIGURATION.md#usb_bluetooth)
  - `usb_audio`: [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_audio),
    [configuration](CONFIGURATION.md#usb_audio)
  - `usb_uart`: [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_uart),
    [configuration](CONFIGURATION.md#usb_uart)
  - `usb_ethernet`:
    [folder](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main/esphome/components/usb_ethernet),
    [configuration](CONFIGURATION.md#usb_ethernet)
- Branches ([`main`](https://github.com/p1ngb4ck/esphome-usb-drivers/tree/main) = the whole topic):
  [`pr/usb-storage`][b-storage], [`pr/usb-cdrom`][b-cdrom], [`pr/usb-hid`][b-hid], [`pr/usb-uvc`][b-uvc],
  [`pr/usb-uvc-video-output`][b-uvc-vo], [`pr/usb-bluetooth`][b-bt], [`pr/usb-audio`][b-audio],
  [`pr/usb-audio-dac`][b-dac], [`pr/usb-audio-media-player`][b-mp], [`pr/usb-uart`][b-uart],
  [`pr/usb-uart-ch934x`][b-ch934x], [`pr/usb-ethernet`][b-eth], [`pr/uart-debug-add-uart-settings`][b-debug]
- Needed repos: [esphome-usb-host](https://github.com/p1ngb4ck/esphome-usb-host) (usb_host),
  [esphome-hid](https://github.com/p1ngb4ck/esphome-hid) (hid),
  [esphome-storage-interface](https://github.com/p1ngb4ck/esphome-storage-interface) (storage),
  [esphome-esp32-platform](https://github.com/p1ngb4ck/esphome-esp32-platform) (esp32)
- Per driver: [esphome-uart-tcp](https://github.com/p1ngb4ck/esphome-uart-tcp) (uart, `usb_uart`),
  [esphome-esp-video](https://github.com/p1ngb4ck/esphome-esp-video) (camera, display, lvgl video output, `usb_uvc`),
  [esphome-audio](https://github.com/p1ngb4ck/esphome-audio) (`usb_audio`),
  [esphome-network](https://github.com/p1ngb4ck/esphome-network) (`usb_ethernet`),
  [esphome-bluetooth](https://github.com/p1ngb4ck/esphome-bluetooth) (`usb_bluetooth`)
- All topics together: `github://p1ngb4ck/esphome@dev`

[b-storage]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-storage
[b-cdrom]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-cdrom
[b-hid]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-hid
[b-uvc]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-uvc
[b-uvc-vo]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-uvc-video-output
[b-bt]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-bluetooth
[b-audio]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-audio
[b-dac]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-audio-dac
[b-mp]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-audio-media-player
[b-uart]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-uart
[b-ch934x]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-uart-ch934x
[b-eth]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/usb-ethernet
[b-debug]: https://github.com/p1ngb4ck/esphome-usb-drivers/tree/pr/uart-debug-add-uart-settings

---

# Detailed version

USB device drivers for ESPHome on the reworked `usb_host`
([esphome-usb-host](https://github.com/p1ngb4ck/esphome-usb-host)): USB drives and optical drives as storages,
keyboards / mice / gamepads / remotes, UVC cameras, Bluetooth dongles, USB sound cards, USB serial adapters and USB
Ethernet adapters. Every driver takes its devices by class - plug a device in and it works, any number at once; `vid`
/ `pid` only pin one particular device to one particular entry.

Chips: ESP32-S2, ESP32-S3, ESP32-P4, ESP32-S31 (per branch below; `usb_storage`, `usb_cdrom`, `usb_uart` also H4).

Needs: `usb_host` from [esphome-usb-host](https://github.com/p1ngb4ck/esphome-usb-host), `esp32` from
[esphome-esp32-platform](https://github.com/p1ngb4ck/esphome-esp32-platform), `storage` from
[esphome-storage-interface](https://github.com/p1ngb4ck/esphome-storage-interface) (drives), `hid` from
[esphome-hid](https://github.com/p1ngb4ck/esphome-hid) (`usb_hid`) - take them from these owners, not from copies in
this repository. Per driver also: `usb_uart` the `uart` of
[esphome-uart-tcp](https://github.com/p1ngb4ck/esphome-uart-tcp); `usb_uvc` lvgl video output, camera and display of
[esphome-esp-video](https://github.com/p1ngb4ck/esphome-esp-video); `usb_bluetooth` for the Bluedroid HCI split:
[esphome-bluetooth](https://github.com/p1ngb4ck/esphome-bluetooth) and `wifi`; `usb_audio` with more than 2
channels: [esphome-audio](https://github.com/p1ngb4ck/esphome-audio);
`usb_ethernet`: [esphome-network](https://github.com/p1ngb4ck/esphome-network).

## Examples

A USB stick and a USB keyboard on an ESP32-S3 or ESP32-P4:

```yaml
external_components:
  - source: github://p1ngb4ck/esphome-usb-drivers@main
    components: [usb_storage, usb_hid, usb_audio]
  - source: github://p1ngb4ck/esphome-usb-host@main
    components: [usb_host]
  - source: github://p1ngb4ck/esphome-storage-interface@main
    components: [storage]
  - source: github://p1ngb4ck/esphome-hid@main
    components: [hid]
  - source: github://p1ngb4ck/esphome-esp32-platform@main
    components: [esp32]

usb_host:
  enable_hubs: true

storage:

usb_storage:
  devices:
    - mount_path: /usb

usb_hid:
  - id: keyboard
    on_text:
      - logger.log:
          format: "typed '%s'"
          args: [text.c_str()]
```

A USB sound card as a speaker (`usb_audio` is in the list above):

```yaml
usb_audio:
  - id: usb_sound

speaker:
  - platform: usb_audio
    usb_audio_id: usb_sound
```

## More

**[CONFIGURATION.md](CONFIGURATION.md)**: every option of every driver with examples - `usb_storage`, `usb_cdrom`,
`usb_hid` (raw reports, buttons), `usb_uvc` (formats, LVGL output, H.264), `usb_bluetooth` (firmware, Bluedroid,
offload), `usb_audio` (speaker, microphone, media player, quirks), `usb_uart` (every adapter type, multi-channel
chips), `usb_ethernet`.

The options every USB driver shares (`vid`, `pid`, `blacklist`) are in esphome-usb-host's
[CONFIGURATION.md](https://github.com/p1ngb4ck/esphome-usb-host/blob/main/CONFIGURATION.md); keyboard layouts and
the input events of `usb_hid` in [esphome-hid](https://github.com/p1ngb4ck/esphome-hid/blob/main/CONFIGURATION.md).

## Branches

One branch per driver or change; `main` contains every branch. Base: ESPHome `dev` at `a5a75738ad`.
UH = `pr/usb-host-refactor` of esphome-usb-host.

| Branch | Change | Chips | Needs |
|---|---|---|---|
| `pr/usb-storage` | `usb_storage`: USB mass storage (1) | S2, S3, S31, P4, H4 | `pr/storage`, UH |
| `pr/usb-cdrom` | `usb_cdrom`: USB optical drives (2) | S2, S3, S31, P4, H4 | `pr/usb-storage` |
| `pr/usb-hid` | `usb_hid`: keyboards, mice, gamepads, remotes (3) | S2, S3, S31, P4 | UH, `pr/hid` |
| `pr/usb-uvc` | `usb_uvc`: USB video class cameras (4) | S2, S3, S31, P4 | UH, `pr/camera-encoder-h264` |
| `pr/usb-uvc-video-output` | `usb_uvc`: frames through the display's video output (5) | P4 | (6) |
| `pr/usb-bluetooth` | `usb_bluetooth`: Bluetooth dongles as HCI controller (7) | S2, S3, S31, P4 | UH, (8) |
| `pr/usb-audio` | `usb_audio` rework (9) | S2, S3, S31, P4 | UH |
| `pr/usb-audio-dac` | `usb_audio` `audio_dac` platform: volume and mute | as `usb_audio` | `pr/usb-audio` |
| `pr/usb-audio-media-player` | `usb_audio` `media_player` platform (10) | as `usb_audio` | `pr/usb-audio` |
| `pr/usb-uart` | `usb_uart` rework (11) | S2, S3, S31, P4, H4 | UH, `pr/uart-runtime-line-settings` |
| `pr/usb-uart-ch934x` | `usb_uart` CH9344 / CH348: multiplexed channels, DTR / RTS | as `usb_uart` | `pr/usb-uart` |
| `pr/usb-ethernet` | `usb_ethernet`: CDC-ECM / -NCM (12) | S2, S3, S31, P4 | UH, `pr/network-usb-ethernet` |
| `pr/uart-debug-add-uart-settings` | `uart` and `usb_uart` `debug_add_uart_settings` (13) | all | `pr/usb-uart` |

1. Bulk-only, SCSI, on the storage interface, FAT / exFAT.
2. ISO 9660 through the storage interface.
3. Any number at once, through the `hid` component.
4. MJPEG over isochronous streams, any number at once.
5. Also the camera encoder option.
6. `pr/usb-uvc`, `pr/camera-encoder-option`, `pr/display-video-output`.
7. Bluedroid on the dongle, firmware download.
8. `pr/esp32-ble-start-order`.
9. Several devices, a format per device, streams open only while used, `feedback`, volume curve and quirks, up to 8
   channels.
10. The `media_player` platform does not load on `main` today: it references symbols upstream ESPHome moved to
    `audio_file`; the fix is pending in esphome-testing.
11. One client for every serial adapter, RX buffer only while connected, runtime line settings.
12. A network interface beside WiFi and Ethernet.
13. The line settings in every debug line.

Branches of other topics, contained as merges: `pr/storage`
([esphome-storage-interface](https://github.com/p1ngb4ck/esphome-storage-interface)), `pr/usb-host-refactor`
([esphome-usb-host](https://github.com/p1ngb4ck/esphome-usb-host)), `pr/hid`
([esphome-hid](https://github.com/p1ngb4ck/esphome-hid)), `pr/camera-encoder-h264`, `pr/camera-encoder-option`,
`pr/display-video-output` ([esphome-esp-video](https://github.com/p1ngb4ck/esphome-esp-video)),
`pr/esp32-ble-start-order` ([esphome-bluetooth](https://github.com/p1ngb4ck/esphome-bluetooth)),
`pr/network-usb-ethernet` ([esphome-network](https://github.com/p1ngb4ck/esphome-network)),
`pr/uart-runtime-line-settings` ([esphome-uart-tcp](https://github.com/p1ngb4ck/esphome-uart-tcp)).

All topics together: `github://p1ngb4ck/esphome@dev`. `external_components` replaces components only; the ESPHome
core of the build host stays as installed.

## License

- ESPHome's own files, also where this repository changes them: the [ESPHome License](LICENSE) (MIT for the Python
  code and everything else, GPLv3 for the C++ runtime files).
- New files written for this repository (new components, their code, tests and documentation): MIT,
  [LICENSE-MIT](LICENSE-MIT).

## Thanks

Thank you, [NonaSuomy](https://github.com/NonaSuomy), for parts of `usb_hid` and many of the ideas behind the USB
HID work. Everyone this project builds on:
[ATTRIBUTIONS.md](https://github.com/p1ngb4ck/esphome/blob/dev/ATTRIBUTIONS.md).

## Branch structure

Each `pr/` branch holds one part of this topic on top of upstream and says in its `BRANCH.md` what it is based on.
`main` holds all of them. UH = `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host, EV = p1ngb4ck/esphome-esp-video.

| Branch | Based on |
|---|---|
| `pr/uart-debug-add-uart-settings` | `pr/usb-uart` |
| `pr/usb-audio` | UH |
| `pr/usb-audio-dac` | `pr/usb-audio` |
| `pr/usb-audio-media-player` | `pr/usb-audio` |
| `pr/usb-bluetooth` | `pr/esp32-ble-start-order` of p1ngb4ck/esphome-bluetooth, UH |
| `pr/usb-cdrom` | `pr/usb-storage` |
| `pr/usb-ethernet` | `pr/network-usb-ethernet` of p1ngb4ck/esphome-network, UH |
| `pr/usb-hid` | `pr/hid` of p1ngb4ck/esphome-hid, UH |
| `pr/usb-storage` | `pr/storage` of p1ngb4ck/esphome-storage-interface, UH |
| `pr/usb-uart` | `pr/uart-runtime-line-settings` of p1ngb4ck/esphome-uart-tcp, UH |
| `pr/usb-uart-ch934x` | `pr/usb-uart` |
| `pr/usb-uvc` | `pr/camera-encoder-h264` of EV, UH |
| `pr/usb-uvc-video-output` | `pr/usb-uvc`, `pr/camera-encoder-option` of EV, `pr/display-video-output` of EV |
