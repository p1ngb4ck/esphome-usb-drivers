# `pr/usb-storage`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_storage`: USB mass storage (bulk-only, SCSI) on the storage interface, FAT / exFAT

Based on these branches, whose changes it holds and builds on:

- `pr/storage` of p1ngb4ck/esphome-storage-interface: the `storage` component: registry, path / filesystem / network / raw storages, the asynchronous worker (jobs, streams), `storage.*` actions, PSRAM transfer buffer, change feed, ISO 9660 mount
- `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host: routing by device class to the drivers (named > vendor table > class, interfaces per driver), several devices per client, one shared USB task, unified transfer engine (bulk, interrupt, control, isochronous streams), driver `blacklist`, host channel budget per controller, the patched espressif/usb component (per-port FIFO bias, `fsls_only`, external hub port requests, root port reset recovery)
