ccflags := -Wall
CFILES = chrdev.c

obj-m += sample.o
sample-objs := $(CFILES:.c=.o)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules V=1

clean:
	make -C /lib/modules/$(shell uname -r)/build M-$(PWD) clean V-1
