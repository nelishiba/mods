KERNEL_DIR = /lib/modules/$(shell uname -r)/build 
BUILD_DIR := $(shell pwd)
VERBOSE := 0 

obj-m := helloworld.o 

all: 
	    make -C $(KERNEL_DIR) SUBDIRS=$(BUILD_DIR) KBUILD_VERBOSE=$(VERBOSE) modules 
clean: 
	    rm -rf *.o *.ko *.mod.c *.symvers *.order .tmp_versions .helloworld.*
