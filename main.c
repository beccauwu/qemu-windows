#define FLAG_IMPLEMENTATION
#include "flag.h"
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"
static char *_program_name = NULL;

static inline char *program_name(void) {
  if(_program_name == NULL) {
    _program_name = realpath(flag_program_name(), NULL);
  }
  return _program_name;
}

void usage(FILE *stream) {
  fprintf(stream, "USAGE: %s [OPTIONS]\n", program_name());
  fprintf(stream, "OPTIONS:\n");
  flag_print_options(stream);
}

static inline bool is_root(void) {
  unsigned int uid = geteuid();
  return uid == 0;
}

#define WIN11_DRIVE "/mnt/gayming/school/win11.qcow2"
#define QEMU_FLAGS                                                            \
  "-M", "q35,usb=on,acpi=on,hpet=off", "-m", "8G", "-cpu",                    \
    "host,hv_relaxed,hv_frequencies,hv_vpindex,hv_ipi,hv_tlbflush,hv_"        \
    "spinlocks=0x1fff,hv_synic,hv_runtime,hv_time,hv_stimer,hv_vapic",        \
    "-vga", "qxl", "-device", "virtio-serial-pci", "-spice",                  \
    "port=5930,disable-ticketing=on", "-device",                              \
    "virtserialport,chardev=spicechannel0,name=com.redhat.spice.0",           \
    "-chardev", "spicevmc,id=spicechannel0,name=vdagent", "-display",         \
    "spice-app", "-smp", "cores=4", "-accel", "kvm", "-device", "usb-tablet", \
    "-nic", "user,model=e1000", "-monitor", "stdio"
int main(int argc, char **argv) {
  Cmd    cmd   = { 0 };
  char **mount = flag_str("mount", NULL, "mount disk to path     [ROOT ONLY]");
  char **umount =
    flag_str("umount", NULL, "unmount disk from path [ROOT ONLY]");
  char **drive = flag_str(
    "drive", "/mnt/gayming/school/win11.qcow2", "qemu qcow2 drive path");
  char **iso =
    flag_str("iso", "/mnt/gayming/school/win11.iso", "path to windows iso");
  bool *help = flag_bool("help", false, "Print this help message");
  if(!flag_parse(argc, argv)) {
    usage(stderr);
    flag_print_error(stderr);
    return 1;
  }
  if(*help) {
    usage(stderr);
    return 0;
  }
  bool first_boot = false;
  if(!file_exists(*drive)) {
    first_boot = true;
    cmd_append(&cmd, "qemu-img", "create", "-f", "qcow2", *drive, "50G");
    if(!cmd_run(&cmd))
      return 1;
  }
  if(*mount) {
    if(!is_root()) {
      nob_log(ERROR, "-mount must be run as root");
      return 1;
    }
    cmd_append(&cmd, "modprobe", "nbd", "max_part=8");
    if(!cmd_run(&cmd))
      return 1;
    cmd_append(&cmd, "qemu-nbd", "--connect=/dev/nbd0", *drive);
    if(!cmd_run(&cmd))
      return 1;
    cmd_append(&cmd, "sudo", "mount", "/dev/nbd0p2", *mount);
    if(!cmd_run(&cmd))
      return 1;
    nob_log(INFO, "mounted disk to %s", *mount);
    return 0;
  }
  if(*umount) {
    if(!is_root()) {
      nob_log(ERROR, "-umount must be run as root");
      return 1;
    }
    cmd_append(&cmd, "umount", *umount);
    if(!cmd_run(&cmd))
      return 1;
    cmd_append(&cmd, "qemu-nbd", "--disconnect", "/dev/nbd0");
    if(!cmd_run(&cmd))
      return 1;
    nob_log(INFO, "unmounted %s", *umount);
    return 0;
  }
  cmd_append(&cmd, "qemu-system-x86_64", QEMU_FLAGS);
  cmd_append(&cmd, "-drive", temp_sprintf("file=%s", *drive));
  if(first_boot) {
    if(!file_exists(*iso)) {
      usage(stderr);
      nob_log(ERROR, "%s doesn't exist", *iso);
      nob_log(INFO, "need to provide valid iso for first boot");
      return 1;
    }
    cmd_append(&cmd, "-cdrom", *iso);
  }

  if(!cmd_run(&cmd))
    return 1;
  return 0;
}