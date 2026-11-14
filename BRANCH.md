# `pr/usb-uvc-video-output`

One part of the topic repo p1ngb4ck/esphome-usb-drivers. The whole topic, with its description, is the branch `main`.

Change of this branch: `usb_uvc`: frames shown through the display's video output, the camera encoder option

Based on these branches, whose changes it holds and builds on:

- `pr/usb-uvc`: `usb_uvc`: USB video class cameras (MJPEG over isochronous streams), any number at once
- `pr/camera-encoder-option` of p1ngb4ck/esphome-esp-video: camera, mipi_csi: the encoder a camera option (`encoder: jpeg \| h264`)
- `pr/display-video-output` of p1ngb4ck/esphome-esp-video: display, lvgl: `display::VideoOutput` - JPEG frames straight into the scanout, LVGL `video_output:` (1-2 canvases), page mode, the camera scanout layer, frame capture
