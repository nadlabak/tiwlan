
PWD := $(shell pwd)
DK_ROOT = ../../../..

EXTRA_CFLAGS += -I$(KERNEL_DIR) -I$(PWD)/$(DK_ROOT)/Txn -I$(PWD)/$(DK_ROOT)/external_drivers/$(HOST_PLATFROM)/Linux/standard_sdio -I$(PWD)/$(DK_ROOT)/platforms/hw/host_platform_sdc4430/Linux/standard_sdio

SDIO_IN_BAND ?= n
TRACE ?= n
STRIP = n
DEBUG ?= y
TEST ?= n

ifeq ($(SDIO_IN_BAND),y)
EXTRA_CFLAGS += -DSDIO_IN_BAND_INTERRUPT
endif

ifeq ($(KERNEL_DEBUGGER),y)
  EXTRA_CFLAGS += -g -O2
endif	
ifeq ($(DEBUG),y)
  EXTRA_CFLAGS += -DSDIO_DEBUG -O2
else
  EXTRA_CFLAGS += -O2
  STRIP = y
endif	

EXTRA_CFLAGS += -DTNETW1283

ifneq ($(KERNELRELEASE),)

obj-m := sdio.o
ifeq ($(TEST),y)
obj-m += testsdio.o
endif

sdio-objs := SdioDrv.o
ifeq ($(TEST),y)
testsdio-objs += testdrv.o
endif
else

PWD := $(shell pwd)
all:
	pwd
	@echo EXTRA_CFLAGS = $(EXTRA_CFLAGS)
	$(MAKE) CROSS_COMPILE=$(CROSS_COMPILE)  EXTRA_CFLAGS="$(EXTRA_CFLAGS)"  ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) modules
ifeq ($(STRIP),y)
	@echo $(CROSS_COMPILE)strip -g sdio.ko
	$(CROSS_COMPILE)strip -g sdio.ko
ifeq ($(TEST),y)
	$(CROSS_COMPILE)strip -g testsdio.ko
endif
endif
endif

clean:
	rm -f *.o *~ *.~* core .depend dep
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

