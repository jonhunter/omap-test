obj-m += omap-test.o
omap-test-objs += main.o timer.o

ccflags-y=-I$(KERNEL_PATH)/arch/arm/mach-omap2/include \
	  -I$(KERNEL_PATH)/arch/arm/plat-omap/include

all:
	make -C $(KERNEL_PATH) M=`pwd` modules

clean:
	make -C $(KERNEL_PATH) M=$(PWD) clean
