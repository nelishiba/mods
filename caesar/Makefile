CFILES = caesar-mod.c

obj-m := caesar.o
caesar-objs := $(CFILES:.c=.o)

ccflags-y := -Wall

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules V=1


clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean V=1
