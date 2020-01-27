all: sse

release:
	RELEASE=1 make clean all 

debug:
	make clean all 

# --- compile flags ---------------------------------------------------

CFLAGS=-Isrc
RFLAGS=-Os -DNDEBUG -Wall
DFLAGS=-g -Wall
LFLAGS=-lcurl

ifeq ($(RELEASE),1)
	CFLAGS:=$(CFLAGS) $(RFLAGS)
else
	CFLAGS:=$(CFLAGS) $(DFLAGS)
endif

# --- shortcuts -------------------------------------------------------

bin:
	mkdir bin

tmp:
	mkdir tmp

sse:  bin bin/sse

clean: 
	rm -rf bin/*

# --- binaries --------------------------------------------------------
bin/sse: src/main.c src/sse.c src/tools.c src/http.c src/parse-sse.c
	gcc $(CFLAGS) -o $@ $^ $(LFLAGS)
ifeq ($(RELEASE),1)
	strip bin/sse
endif

src/parse-sse.c: src/parse-sse.fl
	flex -I -o $@ $<
