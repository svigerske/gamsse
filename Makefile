all : gamsse

gamsse : gamsse.o gmomcc.o gevmcc.o

clean:
	rm -f *.o gamsse

%.c : gams/apifiles/C/api/%.c
	cp $< $@

LDFLAGS = -ldl -Wl,-rpath,\$$ORIGIN -Wl,-rpath,$(realpath gams)
CFLAGS = -Igams/apifiles/C/api -g
