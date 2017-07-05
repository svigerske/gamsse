all : gamsse

gamsse : main.o gamsse.o convert.o cJSON.o base64encode.o gmomcc.o gevmcc.o optcc.o

clean:
	rm -f *.o gamsse

%.c : gams/apifiles/C/api/%.c
	cp $< $@

LDFLAGS = -ldl -Wl,-rpath,\$$ORIGIN -Wl,-rpath,$(realpath gams)
CFLAGS = -Igams/apifiles/C/api -g
CFLAGS += -Wall -Wextra -Wno-unused-parameter
# define _XOPEN_SOURCE to get strptime, define _DEFAULT_SOURCE to get timegm
CFLAGS += -D_XOPEN_SOURCE=500 -D_DEFAULT_SOURCE -std=c99

LDFLAGS += `curl-config --libs`
CFLAGS += `curl-config --cflags`
