# `pr/usb-uvc`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_uvc`: USB video class cameras (MJPEG over isochronous streams), any number at once

Based on these branches, whose changes it holds and builds on:

- `pr/camera-encoder-h264` of p1ngb4ck/esphome-esp-video: camera, camera_encoder: H.264 streams on the P4's hardware encoder
- `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host: routing by device class to the drivers (named > vendor table > class, interfaces per driver), several devices per client, one shared USB task, unified transfer engine (bulk, interrupt, control, isochronous streams), driver `blacklist`, host channel budget per controller, the patched espressif/usb component (per-port FIFO bias, `fsls_only`, external hub port requests, root port reset recovery)
