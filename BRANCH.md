# `pr/usb-bluetooth`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_bluetooth`: Bluetooth dongles as HCI controller - Bluedroid on the dongle, firmware download

Based on these branches, whose changes it holds and builds on:

- `pr/esp32-ble-start-order` of p1ngb4ck/esphome-bluetooth: Bluedroid starts once WiFi reports its start-up done and, with a usb_bluetooth bluedroid dongle, once the dongle is ready; `restart()`
- `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host: routing by device class to the drivers (named > vendor table > class, interfaces per driver), several devices per client, one shared USB task, unified transfer engine (bulk, interrupt, control, isochronous streams), driver `blacklist`, host channel budget per controller, the patched espressif/usb component (per-port FIFO bias, `fsls_only`, external hub port requests, root port reset recovery)
