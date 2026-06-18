# glb2icn Makefile - PC-side offline converter (.glb -> PS2 .icn save icon)
#
# Header-only deps (in the spirit of dcmesh's cloned meshoptimizer):
#   cgltf.h               - GLB loader. Reused from dcmesh by default, or clone
#                           https://github.com/jkuhlmann/cgltf
#   stb_image.h           - image decode (PNG/JPEG)   } from
#   stb_image_resize2.h   - image resize to 128x128    } https://github.com/nothings/stb
#
# Build:
#   git clone https://github.com/nothings/stb tools/glb2icn/stb   (or set STB_DIR)
#   make                       (CGLTF_DIR defaults to dcmesh's copy)
#   make STB_DIR=/path/to/stb CGLTF_DIR=/path/to/cgltf

CGLTF_DIR ?= ../dcmesh/meshoptimizer/extern
STB_DIR   ?= ./stb

CC     = gcc
CFLAGS = -O2 -Wall -Wextra -I$(CGLTF_DIR) -I$(STB_DIR) -Isrc

ifeq ($(OS),Windows_NT)
EXEEXT ?= .exe
endif

TARGET = glb2icn$(EXEEXT)

all: $(TARGET)

$(TARGET): src/glb2icn.c
	$(CC) $(CFLAGS) src/glb2icn.c -o $(TARGET) -lm

clean:
	rm -f glb2icn glb2icn.exe

.PHONY: all clean
