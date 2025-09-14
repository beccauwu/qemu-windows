#define FLAG_IMPLEMENTATION
#include "flag.h"
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define QEMU_AUDIO_BACKEND "pipewire"

#define QEMU_FLAGS                                                             \
  "-M", "q35,usb=on,acpi=on,hpet=off", "-m", "8G", "-cpu",                     \
    "host,hv_relaxed,hv_frequencies,hv_vpindex,hv_ipi,hv_tlbflush,hv_"         \
    "spinlocks=0x1fff,hv_synic,hv_runtime,hv_time,hv_stimer,hv_vapic",         \
    "-device", "ich9-intel-hda", "-device", "hda-duplex,audiodev=snd0",        \
    "-audiodev", QEMU_AUDIO_BACKEND ",id=snd0", "-vga", "qxl", "-device",      \
    "virtio-serial-pci", "-spice", "port=5930,disable-ticketing=on",           \
    "-device", "virtserialport,chardev=spicechannel0,name=com.redhat.spice.0", \
    "-chardev", "spicevmc,id=spicechannel0,name=vdagent", "-display",          \
    "spice-app", "-smp", "cores=4", "-accel", "kvm", "-device", "usb-tablet",  \
    "-usb", "-device", "usb-ehci,id=ehci", "-monitor", "stdio"

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
  fprintf(stream, "NOTE:\n");
  fprintf(stream, " - do not start VM as sudo, otherwise audio won't work.\n");
  fprintf(
    stream,
    " - modifies permissions for /dev/bus/<camera> if `-camera` provided\n");
}

static inline bool is_root(void) {
  unsigned int uid = geteuid();
  return uid == 0;
}

bool get_camera_bus_addr(const char  *cam,
                         const char **bus_out,
                         const char **addr_out) {
  Cmd cmd = { 0 };
  cmd_append(&cmd, "lsusb", "-d", cam);
  const char *of = "/tmp/qemu_win_camera_lsusb";
  if(!cmd_run(&cmd, .stdout_path = of)) {
    nob_log(ERROR, "couldn't find camera with id '%s'", cam);
    return false;
  }
  String_Builder sb = { 0 };
  if(!read_entire_file(of, &sb))
    return false;
  // Bus <bus> Device <addr>: ID <id> <name>
  String_View sv = sb_to_sv(sb);
  sv_chop_by_delim(&sv, ' ');
  String_View bus = sv_chop_by_delim(&sv, ' ');
  sv_chop_by_delim(&sv, ' ');
  String_View addr = sv_chop_by_delim(&sv, ':');
  nob_log(INFO,
          "found camera with id '%s' at bus '" SV_Fmt "', addr '" SV_Fmt "'",
          cam,
          SV_Arg(bus),
          SV_Arg(addr));
  *bus_out  = temp_sv_to_cstr(bus);
  *addr_out = temp_sv_to_cstr(addr);
  return true;
}

Cmd cmd = { 0 };

bool mount_drive(const char *drive, const char *path) {
  cmd_append(&cmd, "sudo", "modprobe", "nbd", "max_part=8");
  if(!cmd_run(&cmd))
    return false;
  cmd_append(&cmd, "sudo", "qemu-nbd", "--connect=/dev/nbd0", drive);
  if(!cmd_run(&cmd))
    return false;
  cmd_append(&cmd, "sudo", "mount", "/dev/nbd0p2", path);
  if(!cmd_run(&cmd))
    return false;
  nob_log(INFO, "mounted disk to '%s'", path);
  return true;
}
bool unmount_drive(const char *path) {
  cmd_append(&cmd, "sudo", "umount", path);
  if(!cmd_run(&cmd))
    return false;
  cmd_append(&cmd, "sudo", "qemu-nbd", "--disconnect", "/dev/nbd0");
  if(!cmd_run(&cmd))
    return false;
  nob_log(INFO, "unmounted '%s'", path);
  return true;
}

bool run_emu(const char *drive, const char *iso, bool net, const char *cam) {
  const char *camera_bus;
  const char *camera_addr;
  if(cam) {
    if(!get_camera_bus_addr(cam, &camera_bus, &camera_addr))
      return false;
    nob_log(INFO, "setting camera permissions...");
    cmd_append(&cmd,
               "sudo",
               "chmod",
               "a+rw",
               temp_sprintf("/dev/bus/usb/%s/%s", camera_bus, camera_addr));
  }
  if(!cmd_run(&cmd))
    return false;
  cmd_append(&cmd, "qemu-system-x86_64", QEMU_FLAGS);
  cmd_append(&cmd, "-drive", temp_sprintf("file=%s", drive));
  if(cam)
    cmd_append(&cmd,
               "-device",
               temp_sprintf("usb-host,hostbus=%s,hostaddr=%s,bus=ehci.0",
                            camera_bus,
                            camera_addr));
  if(net)
    cmd_append(&cmd, "-nic", "user,model=e1000");
  if(iso != NULL) {
    if(!file_exists(iso)) {
      usage(stderr);
      nob_log(ERROR, "'%s' doesn't exist", iso);
      return false;
    }
    cmd_append(&cmd, "-cdrom", iso);
  }

  if(!cmd_run(&cmd))
    return false;
  return true;
}

int main(int argc, char **argv) {
  char **drive  = flag_str("drive", NULL, "qemu drive path");
  char **iso    = flag_str("iso", NULL, "path to windows iso");
  bool  *mount  = flag_bool("mount", false, "mount <drive> to <path>");
  bool  *umount = flag_bool("umount", false, "unmount disk from <path>");
  char **path   = flag_str("path", NULL, "mount point for <drive>");
  char **camera_id =
    flag_str("camera", NULL, "camera id from lsusb (in format 123f:bd32)");
  bool *make = flag_bool(
    "make", false, "create <drive> before boot (needs valid iso to boot)");
  bool *nonet = flag_bool("nonet", false, "don't add a network card");
  bool *help  = flag_bool("help", false, "Print this help message");
  if(!flag_parse(argc, argv)) {
    usage(stderr);
    flag_print_error(stderr);
    return 1;
  }
  if(*help) {
    usage(stderr);
    return 0;
  }
  if((*umount || *mount) && *path == NULL) {
    usage(stderr);
    nob_log(ERROR, "no mount point provided");
    return 1;
  }
  if(*umount) {
    if(!unmount_drive(*path))
      return 1;
    return 0;
  }
  if(*drive == NULL) {
    nob_log(ERROR, "no drive provided");
    return 1;
  }
  if(*make) {
    if(file_exists(*drive))
      nob_log(INFO, "'%s' already exists", *drive);
    else {
      if(*iso == NULL) {
        usage(stderr);
        nob_log(ERROR, "no iso provided");
        return 1;
      }
      cmd_append(&cmd, "qemu-img", "create", "-f", "qcow2", *drive, "50G");
      if(!cmd_run(&cmd))
        return 1;
    }
  }
  if(*mount) {
    if(!mount_drive(*drive, *path))
      return 1;
    return 0;
  }
  if(is_root()) {
    nob_log(ERROR, "do not run VM as root.");
    return 1;
  }

  if(!run_emu(*drive, *iso, !nonet, *camera_id))
    return 1;
  return 0;
}