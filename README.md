# Qemu to boot windows

mostly for me to be able to use native office apps for school since im not on windows. needed more functionality than a bash script, so i made this. Runs qemu with audio input/output and camera, spice protocol for display.

Might be useful if you need to run windows in qemu.

note: runs lots of things as sudo, changes permissions on `/dev/bus/usb/<bus>`/`<device>` (if `-nocam` isn't provied, otherwise qemu can't use camera)

this might also work for other os's than windows, i'm just not sure about the partitions for mounting.

## Dependencies

* qemu
* virt-viewer (or some other spice client)

## Building

```bash
make
sudo make install
```

I then make a shell alias with options set for the specific vm

```bash
# ~/.bashrc
alias myvm='qemu-windows -drive myvm.qcow2 -path /mnt/myvm -nonet -camera 123f:bd32'

$ myvm -iso ./windows10.iso -make
```
