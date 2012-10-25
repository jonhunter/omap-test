obj-m += omap-test.o
omap-test-objs += main.o timer.o

all:
	make -C $(KERNEL_PATH) M=`pwd` modules

clean:
	make -C $(KERNEL_PATH) M=$(PWD) clean
