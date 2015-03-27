kver   = `uname -r`
ksrc   = "/lib/modules/$(kver)/build"
sysr   = "$(PWD)/../../sysroot"
mdir   = "modtest"

obj-m += gpiomod_dual_hcsr04.o

all:
	make -C $(ksrc) M=$(PWD) modules

modules_install:
	make -C $(ksrc) M=$(PWD) INSTALL_MOD_PATH=$(sysr) INSTALL_MOD_DIR=$(mdir) modules_install

node:
	sudo mknod /dev/dual_hcsr04 c 119 0
rmnode:
	sudo rm /dev/dual_hcsr04

clean:
	make -C $(ksrc) M=$(PWD) clean
