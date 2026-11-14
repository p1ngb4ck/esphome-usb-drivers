# `pr/usb-uvc-video-output`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_uvc`: frames shown through the display's video output, the camera encoder option

Based on these branches, whose changes it holds and builds on:

- `pr/camera-encoder-option` of p1ngb4ck/esphome-esp-video: camera, mipi_csi: the encoder a camera option (`encoder: jpeg \| h264`)
- `pr/display-video-output` of p1ngb4ck/esphome-esp-video: display, lvgl: `display::VideoOutput` - JPEG frames straight into the scanout, LVGL `video_output:` (1-2 canvases), page mode, the camera scanout layer, frame capture
- `pr/usb-host-refactor` of p1ngb4ck/esphome-usb-host: routing by device class to the drivers (named > vendor table > class, interfaces per driver), several devices per client, one shared USB task, unified transfer engine (bulk, interrupt, control, isochronous streams), driver `blacklist`, host channel budget per controller, the patched espressif/usb component (per-port FIFO bias, `fsls_only`, external hub port requests, root port reset recovery)
