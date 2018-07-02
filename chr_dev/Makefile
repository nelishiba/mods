ccflags-y := -Wall -ggdb -DDEBUG
CFILES = chrdev.c
CC += $(ccflags-y)

obj-m += sample.o
sample-objs := $(CFILES:.c=.o)

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules V=1

debug:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 
	EXTRA_CFLAGS="$(ccflags-y)"

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean V=1
