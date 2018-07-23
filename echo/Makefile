obj-m := kecho.o
kecho-objs := $(CFILES:.c=.o)

ccflags-y := -Wall
CC = gcc

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules V=1
	make client

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean V=1
	rm -rf client

client: client.o
	$(CC) -o $@ $<

kecho.o: myheader.h
client.o: myheader.h
