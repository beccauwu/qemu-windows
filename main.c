#include <asm-generic/errno-base.h>
#define FLAG_IMPLEMENTATION
#include "flag.h"
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define QEMU_AUDIO_BACKEND "pipewire"

#define QEMU_FLAGS                                                            \
  "-M", "q35,usb=on,acpi=on,hpet=off", "-cpu",                                \
    "host,hv_relaxed,hv_frequencies,hv_vpindex,hv_ipi,hv_tlbflush,hv_"        \
    "spinlocks=0x1fff,hv_synic,hv_runtime,hv_time,hv_stimer,hv_vapic",        \
    "-vga", "qxl", "-device", "virtio-serial-pci", "-spice",                  \
    "port=5930,disable-ticketing=on", "-device",                              \
    "virtserialport,chardev=spicechannel0,name=com.redhat.spice.0",           \
    "-chardev", "spicevmc,id=spicechannel0,name=vdagent", "-display",         \
    "spice-app", "-smp", "cores=4", "-accel", "kvm", "-device", "usb-tablet", \
    "-usb", "-device", "usb-ehci,id=ehci", "-monitor", "stdio"

static struct {
  char *mem;
  char *mount_point;
  char *iso;
  char *drive;
  char *cam;
  bool  nonet;
  bool  noaud;
  bool  mount;
  bool  umount;
  bool  make;
} args = { 0 };

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
  if(!sv_eq(sv_chop_by_delim(&sv, ' '), sv_from_cstr("Bus"))) {
    nob_log(ERROR, "lsusb output malformed");
    abort();
  }
  String_View bus = sv_chop_by_delim(&sv, ' ');
  if(!sv_eq(sv_chop_by_delim(&sv, ' '), sv_from_cstr("Device"))) {
    nob_log(ERROR, "lsusb output malformed");
    abort();
  }
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

bool mount_drive() {
  cmd_append(&cmd, "sudo", "modprobe", "nbd", "max_part=8");
  if(!cmd_run(&cmd))
    return false;
  cmd_append(&cmd, "sudo", "qemu-nbd", "--connect=/dev/nbd0", args.drive);
  if(!cmd_run(&cmd))
    return false;
  cmd_append(&cmd, "sudo", "mount", "/dev/nbd0p2", args.mount_point);
  if(!cmd_run(&cmd))
    return false;
  nob_log(INFO, "mounted disk to '%s'", args.mount_point);
  return true;
}
bool unmount_drive() {
  cmd_append(&cmd, "sudo", "umount", args.mount_point);
  if(!cmd_run(&cmd))
    return false;
  cmd_append(&cmd, "sudo", "qemu-nbd", "--disconnect", "/dev/nbd0");
  if(!cmd_run(&cmd))
    return false;
  nob_log(INFO, "unmounted '%s'", args.mount_point);
  return true;
}

bool run_emu() {
  const char *camera_bus;
  const char *camera_addr;
  if(args.cam) {
    if(!get_camera_bus_addr(args.cam, &camera_bus, &camera_addr))
      return false;
    const char *bus_file =
      temp_sprintf("/dev/bus/usb/%s/%s", camera_bus, camera_addr);
    if(access(bus_file, R_OK | W_OK) != 0) {
      if(errno != EACCES) {
        nob_log(ERROR,
                "cannot set permissions for '%s': %s",
                bus_file,
                strerror(errno));
        nob_log(INFO, "retry boot without camera or fix above error");
        return false;
      }
      nob_log(INFO, "setting permissions for '%s'...", bus_file);
      cmd_append(&cmd, "sudo", "chmod", "a+rw", bus_file);
      if(!cmd_run(&cmd))
        return false;
    } else {
      nob_log(INFO, "'%s' already has correct permissions!", bus_file);
    }
  }

  cmd_append(&cmd, "qemu-system-x86_64", QEMU_FLAGS);
  cmd_append(&cmd, "-drive", temp_sprintf("file=%s", args.drive));
  cmd_append(&cmd, "-m", args.mem);

  if(args.cam)
    cmd_append(&cmd,
               "-device",
               temp_sprintf("usb-host,hostbus=%s,hostaddr=%s,bus=ehci.0",
                            camera_bus,
                            camera_addr));

  if(!args.noaud)
    cmd_append(&cmd,
               "-device",
               "ich9-intel-hda",
               "-device",
               "hda-duplex,audiodev=snd0",
               "-audiodev",
               QEMU_AUDIO_BACKEND ",id=snd0");

  if(args.nonet)
    cmd_append(&cmd, "-nic", "none");
  else
    cmd_append(&cmd, "-nic", "user,model=e1000");

  if(args.iso != NULL) {
    if(!file_exists(args.iso)) {
      usage(stderr);
      nob_log(ERROR, "'%s' doesn't exist", args.iso);
      return false;
    }
    cmd_append(&cmd, "-cdrom", args.iso);
  }

  if(!cmd_run(&cmd))
    return false;
  return true;
}
#define streq(s1, s2) (strcmp((s1), (s2)) == 0)

bool parse_args(int *argc, char ***argv) {
  flag_str_var(&args.drive, "drive", NULL, "qemu drive path");
  flag_str_var(&args.iso, "iso", NULL, "path to windows iso");
  flag_bool_var(&args.mount, "mount", false, "mount <drive> to <path>");
  flag_bool_var(&args.umount, "umount", false, "unmount disk from <path>");
  flag_str_var(&args.mount_point, "path", NULL, "mount point for <drive>");
  flag_str_var(
    &args.cam, "camera", NULL, "camera id from lsusb (in format 123f:bd32)");
  flag_str_var(&args.mem, "m", "8G", "amount of memory for VM");
  flag_bool_var(&args.make,
                "make",
                false,
                "create <drive> before boot (needs valid iso to boot)");
  Flag_List *nos  = flag_list("no",
                             "what to disable\n"
                              "        supported options:\n"
                              "          aud/audio   - audio device\n"
                              "          net/network - network device");
  bool      *help = flag_bool("help", false, "Print this help message");
  for(size_t i = 0; i < nos->count; ++i) {
    if(streq(nos->items[i], "aud") || streq(nos->items[i], "audio")) {
      args.noaud = true;
    }
    if(streq(nos->items[i], "net") || streq(nos->items[i], "network")) {
      args.nonet = true;
    }
  }
  if(!flag_parse(*argc, *argv)) {
    usage(stderr);
    flag_print_error(stderr);
    return false;
  }
  if(*help) {
    usage(stderr);
    exit(0);
  }
  return true;
}

int main(int argc, char **argv) {
  if(!parse_args(&argc, &argv))
    return 1;
  if((args.umount || args.mount) && args.mount_point == NULL) {
    usage(stderr);
    nob_log(ERROR, "no mount point provided");
    return 1;
  }
  if(args.umount) {
    if(!unmount_drive())
      return 1;
    return 0;
  }
  if(args.drive == NULL) {
    nob_log(ERROR, "no drive provided");
    return 1;
  }
  if(args.make) {
    if(file_exists(args.drive))
      nob_log(INFO, "'%s' already exists", args.drive);
    else {
      if(args.iso == NULL) {
        usage(stderr);
        nob_log(ERROR, "no iso provided");
        return 1;
      }
      cmd_append(&cmd, "qemu-img", "create", "-f", "qcow2", args.drive, "50G");
      if(!cmd_run(&cmd))
        return 1;
    }
  }
  if(args.mount) {
    if(!mount_drive())
      return 1;
    return 0;
  }
  if(is_root()) {
    nob_log(ERROR, "do not run VM as root.");
    return 1;
  }

  if(!run_emu())
    return 1;
  return 0;
}