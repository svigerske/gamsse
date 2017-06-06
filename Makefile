all : gamsse

gamsse : gamsse.o convert.o cJSON.o gmomcc.o gevmcc.o

clean:
	rm -f *.o gamsse

%.c : gams/apifiles/C/api/%.c
	cp $< $@

LDFLAGS = -ldl -Wl,-rpath,\$$ORIGIN -Wl,-rpath,$(realpath gams)
CFLAGS = -Igams/apifiles/C/api -g -Wall -Wextra -Wno-unused-parameter

LDFLAGS += `curl-config --libs`
CFLAGS += `curl-config --cflags`
