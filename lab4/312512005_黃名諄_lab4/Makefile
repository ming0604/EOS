obj-m := mydev.o

all: RPI_build #or RPI_build, you can specify only one build at a time

clean: x86_clean RPI_clean

x86_build: 
	sudo make -C /lib/modules/6.8.0-45-generic/build M=$(shell pwd) modules
x86_clean:
	sudo make -C /lib/modules/6.8.0-45-generic/build M=$(shell pwd) clean
RPI_build: 
	sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C /home/willy/linux M=$(shell pwd) modules
RPI_clean:
	sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C /home/willy/linux M=$(shell pwd) clean
.PHONY: all clean x86_build x86_clean RPI_build RPI_clean