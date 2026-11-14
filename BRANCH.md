# `pr/usb-hid`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_hid`: keyboards, mice, gamepads, remotes, any number at once, through the `hid` component

Based on these branches, whose changes it holds and builds on:

- `pr/hid` of p1ngb4ck/esphome-hid: `hid`: report maps, keyboard layouts (also as files), keyboards, mice, gamepads, remotes; key events carry their device; `on_key`, `on_text`, `on_pointer`, `on_control`, API actions
- `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host: routing by device class to the drivers (named > vendor table > class, interfaces per driver), several devices per client, one shared USB task, unified transfer engine (bulk, interrupt, control, isochronous streams), driver `blacklist`, host channel budget per controller, the patched espressif/usb component (per-port FIFO bias, `fsls_only`, external hub port requests, root port reset recovery)
