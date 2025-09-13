# Windows for my school use

mostly for me to be able to use native office apps for school since im not on windows. needed more functionality than a bash script, so i made this. Runs qemu with audio input/output and camera, spice protocol for display.

Might be useful if you need to run windows in qemu. Just change `CAMERA_ID` definition to whatever `lsusb` gives as integrated camera id (might also work for external camera?), you can also change `DRIVE_ENV` and `SCHOOL_DRIVE_MOUNT` to whatever you want to put in your env :3

note: runs lots of things as sudo, changes permissions on `/dev/bus/usb/<bus>`/`<device>` (otherwise qemu can't use camera)

## Dependencies

* qemu
* virt-viewer (or some other spice client)

## Building

```bash
make
sudo make install
```
