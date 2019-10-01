obj-m = s3c24xx_buttons_driver.o
K_DIR = /home/user/Desktop/emb/linux-3.4.2/
PWD = $(shell pwd)

all:
	#arm-linux-gcc -static -c cameraFunc.c
	arm-linux-gcc -static -c cameraMain.c
	arm-linux-gcc -static -o camera cameraMain.o
	arm-linux-gcc -static -o button buttonMain.c
	make -C $(K_DIR) M=$(PWD) modules
	rm -rf *.o *.mod *.order *.sym* *.mod.c 
clean:
	make -C $(K_DIR) M=$(PWD) clean
	rm -rf camera button *.ko
