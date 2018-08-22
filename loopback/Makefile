obj-m := snull.o
snull-objs := $(CFILES:.c=.o)

ccflags-y := -Wall
CC = gcc

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules V=1
	
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean V=1


