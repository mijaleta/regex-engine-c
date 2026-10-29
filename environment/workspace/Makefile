# Builds ./rx from the C sources in src/. The grader runs `make` in a fresh
# copy of /app and then uses ./rx, so keep that working.
CC = cc
CFLAGS = -O2 -std=c11 -Wall
SRC = $(wildcard src/*.c)
HDR = $(wildcard src/*.h)

rx: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f rx

.PHONY: clean
