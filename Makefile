
CROSS_COMPILE :=/opt/v3s/spinand/lichee/out/sun8iw8p1/linux/common/buildroot/external-toolchain/bin/arm-linux-gnueabi-
#CROSS_COMPILE :=/home/w/tmp_mine/tmp/toolchain/gcc-linaro-4.9-2016.02-x86_64_arm-linux-gnueabi/bin/arm-linux-gnueabi-

LIB_DIR :=/home/w/tmp_mine/v3s_lib/out
CC :=$(CROSS_COMPILE)gcc
ifdef DEBUG
    CFLAGS = -Wall -pthread -g -D_GNU_SOURCE
else
    CFLAGS = -Wall -pthread -O2 -D_GNU_SOURCE
endif

PREFIX = /usr/local/
LDFLAGS := --static
CFLAGS += -I$(LIB_DIR)/include -std=c99
LD_JPEG := -ljpeg -L$(LIB_DIR)/lib -Wl,-rpath,/data/lib/
all: streameye camStream dualCamStream

yuyv2rgb.o: yuyv2rgb.c
	$(CC) $(CFLAGS) -c -o yuyv2rgb.o yuyv2rgb.c

camStream.o: camStream.c
	$(CC) $(CFLAGS) -c -o camStream.o camStream.c

dualCamStream.o: dualCamStream.c
	$(CC) $(CFLAGS) -c -o dualCamStream.o dualCamStream.c

streameye.o: streameye.c streameye.h client.h common.h
	$(CC) $(CFLAGS) -c -o streameye.o streameye.c

client.o: client.c client.h streameye.h common.h
	$(CC) $(CFLAGS) -c -o client.o client.c

auth.o: auth.c auth.h  common.h
	$(CC) $(CFLAGS) -c -o auth.o auth.c

streameye: streameye.o client.o auth.o
	$(CC) $(CFLAGS) -o streameye streameye.o client.o auth.o $(LDFLAGS)

camStream: camStream.o yuyv2rgb.o
	$(CC) $(CFLAGS) -o camStream camStream.o yuyv2rgb.o $(LD_JPEG) -lpthread

dualCamStream: dualCamStream.o yuyv2rgb.o
	$(CC) $(CFLAGS) -o dualCamStream dualCamStream.o yuyv2rgb.o $(LD_JPEG) -lpthread

install: streameye
	cp streameye $(PREFIX)/bin

clean:
	rm -f *.o
	rm -f streameye camStream
	
