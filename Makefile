CC = gcc
CFLAGS = -O3 -march=native -Wall -Wextra -std=c11 -Isrc
LDFLAGS = -lm
SRC = src/main.c
OUT = glt

all: $(OUT)

$(OUT): $(SRC) src/vec.h src/ray.h src/camera.h src/image.h src/glt.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(OUT) output.ppm

.PHONY: all clean
