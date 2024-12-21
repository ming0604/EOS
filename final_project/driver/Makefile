obj-m := final_project_driver.o

all: RPI_build

clean: x86_clean RPI_clean

x86_build: final_project_driver.c
	sudo make -C /lib/modules/6.8.0-45-generic/build M=$(PWD) modules

x86_clean:
	sudo make -C /lib/modules/6.8.0-45-generic/build M=$(PWD) clean

RPI_build: final_project_driver.c
	sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C /home/wei/EOS/linux M=$(PWD) modules

RPI_clean:
	sudo make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C /home/wei/EOS/linux M=$(PWD) clean

.PHONY: all clean x86_build x86_clean RPI_build RPI_clean