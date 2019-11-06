ARCH := $(shell uname -m)
ifeq ($(ARCH), x86_64)
TE_SDK_LIBS = -li3system_te_64 -li3system_usb_64
else
TE_SDK_LIBS = -li3system_te_32 -li3system_usb_32
endif

all: teq1drv thermImg.so
teq1drv: teq1drv.o
	gcc -Wl,--rpath -Wl,lib -Wl,--rpath -Wl,/ssd/teq1/lib -o $@ $< -L lib $(TE_SDK_LIBS) -lusb-1.0 -lpthread
teq1drv.o: teq1drv.cpp
	gcc -I include -D _POSIX_SOURCE -O2 -g -c $<
thermImg.so: thermimg.o
	gcc -shared -O2 -Wall -g -o $@ $<
thermimg.o: thermimg.c thermimg_impl.c
	gcc -I/usr/include/tcl8.6 -fPIC -O2 -Wall -g -c $< -o $@
