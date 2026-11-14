# Makefile — backend legacy_4x (kbuild multi-file)
# Berada SEFOLDER dengan sumber (kernel/) sesuai konvensi kbuild.
obj-m += rkit4x.o
rkit4x-y := core.o syscall_hook.o hooks.o hook_seq.o module_hide.o backdoor.o

KVER ?= $(shell uname -r)
KDIR := /lib/modules/$(KVER)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
