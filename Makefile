ifneq ($(KERNELRELEASE),)
obj-m := test_gpio.o
else
#KDIR := ../../../../src/linux
all:
	$(MAKE) -C $(KDIR) M=$$PWD
clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
endif

install:
	cp ./test_gpio.ko $(rpi_output)/br_shadow/target/root
	@echo "./test_gpio.ko is installed to $(rpi_output)/br_shadow/target/root"
