ksrc   = "~/linux"
sysr   = "$(PWD)/../../sysroot"
mdir   = "modtest"

obj-m += gpiomod_dual_hcsr04.o

all:
	make -C $(ksrc) M=$(PWD) modules

modules_install:
	make -C $(ksrc) M=$(PWD) INSTALL_MOD_PATH=$(sysr) INSTALL_MOD_DIR=$(mdir) modules_install

clean:
	make -C $(ksrc) M=$(PWD) clean
