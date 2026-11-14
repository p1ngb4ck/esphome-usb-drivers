# `pr/usb-uart`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_uart` rework: one client for every serial adapter, RX buffer only while connected, runtime line settings

Based on these branches, whose changes it holds and builds on:

- `pr/uart-runtime-line-settings` of p1ngb4ck/esphome-uart-tcp: uart: line settings settable at runtime and saved (ESP32, ESP8266)
- `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host: routing by device class to the drivers (named > vendor table > class, interfaces per driver), several devices per client, one shared USB task, unified transfer engine (bulk, interrupt, control, isochronous streams), driver `blacklist`, host channel budget per controller, the patched espressif/usb component (per-port FIFO bias, `fsls_only`, external hub port requests, root port reset recovery)
