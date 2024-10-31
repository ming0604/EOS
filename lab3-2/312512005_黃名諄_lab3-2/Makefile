obj-m := lab3-2_driver.o

all: 
	sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C /home/willy/linux M=$(shell pwd) modules
clean:
	sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C /home/willy/linux M=$(shell pwd) clean
	
