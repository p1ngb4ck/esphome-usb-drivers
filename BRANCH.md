# `pr/usb-ethernet`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_ethernet`: CDC-ECM / CDC-NCM adapters as a network interface beside WiFi and Ethernet

Based on these branches, whose changes it holds and builds on:

- `pr/network-usb-ethernet` of p1ngb4ck/esphome-network: usb_ethernet as a third arbitrated interface (network, mdns)
- `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host: routing by device class to the drivers (named > vendor table > class, interfaces per driver), several devices per client, one shared USB task, unified transfer engine (bulk, interrupt, control, isochronous streams), driver `blacklist`, host channel budget per controller, the patched espressif/usb component (per-port FIFO bias, `fsls_only`, external hub port requests, root port reset recovery)
