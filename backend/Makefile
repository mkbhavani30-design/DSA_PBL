CC ?= gcc
CFLAGS := -O2 -Wall -Wextra -std=c11 -I../engine

ENGINE_SRCS := \
	../engine/campus.c \
	../engine/heap.c \
	../engine/campus_router.c \
	../engine/segment_tree.c \
	../engine/map_loader.c \
	../engine/engine_api.c

BACKEND_SRCS := server.c

ifeq ($(OS),Windows_NT)
	TARGET := campus_backend.exe
	LDLIBS := -lm -lws2_32
else
	TARGET := campus_backend
	LDLIBS := -lm
endif

all: $(TARGET)

$(TARGET): $(BACKEND_SRCS) $(ENGINE_SRCS)
	$(CC) $(CFLAGS) $(BACKEND_SRCS) $(ENGINE_SRCS) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET) campus_backend.exe

.PHONY: all clean