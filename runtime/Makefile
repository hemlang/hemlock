# Hemlock Runtime Library Makefile

CC = gcc
LDFLAGS = -lm -lpthread -lffi

# Platform-specific settings
ifeq ($(shell uname),Darwin)
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -fPIC -Iinclude -D_DARWIN_C_SOURCE
    # Detect libffi (Homebrew on macOS puts it in non-standard locations)
    BREW_LIBFFI := $(shell brew --prefix libffi 2>/dev/null)
    ifneq ($(BREW_LIBFFI),)
        CFLAGS += -I$(BREW_LIBFFI)/include
        LDFLAGS += -L$(BREW_LIBFFI)/lib
    endif
    # Detect OpenSSL
    BREW_OPENSSL := $(shell brew --prefix openssl 2>/dev/null)
    ifneq ($(BREW_OPENSSL),)
        CFLAGS += -I$(BREW_OPENSSL)/include
        LDFLAGS += -L$(BREW_OPENSSL)/lib
    endif
else
    CFLAGS = -Wall -Wextra -std=c11 -O3 -g -fPIC -Iinclude -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
    LIBFFI_CFLAGS := $(shell pkg-config --cflags libffi 2>/dev/null)
    LIBFFI_LIBS := $(shell pkg-config --libs libffi 2>/dev/null)
    ifneq ($(LIBFFI_CFLAGS),)
        CFLAGS += $(LIBFFI_CFLAGS)
        LDFLAGS += $(LIBFFI_LIBS)
    endif
endif

# Check if zlib is available (both header AND library must exist)
ZLIB_CHECK := $(shell echo 'int main(){return 0;}' | $(CC) -x c - -lz -o /dev/null 2>/dev/null && echo yes)
ifeq ($(ZLIB_CHECK),yes)
    CFLAGS += -DHML_HAVE_ZLIB
    LDFLAGS += -lz
endif

# Check if libwebsockets is available
LWS_CHECK := $(shell pkg-config --exists libwebsockets 2>/dev/null && echo yes)
ifeq ($(LWS_CHECK),yes)
    CFLAGS += -DHML_HAVE_LIBWEBSOCKETS $(shell pkg-config --cflags libwebsockets)
    LDFLAGS += $(shell pkg-config --libs libwebsockets)
else
    # Fallback: check if header exists directly
    LWS_HEADER_CHECK := $(shell test -f /usr/include/libwebsockets.h && echo yes)
    ifeq ($(LWS_HEADER_CHECK),yes)
        CFLAGS += -DHML_HAVE_LIBWEBSOCKETS
        LDFLAGS += -lwebsockets -lssl -lcrypto
    endif
endif

SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Library targets
STATIC_LIB = libhemlock_runtime.a
SHARED_LIB = libhemlock_runtime.so

.PHONY: all clean static shared

all: $(BUILD_DIR) static shared

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Static library
static: $(BUILD_DIR)/$(STATIC_LIB)

$(BUILD_DIR)/$(STATIC_LIB): $(OBJS)
	ar rcs $@ $^
	@echo "Built static library: $@"

# Shared library
shared: $(BUILD_DIR)/$(SHARED_LIB)

$(BUILD_DIR)/$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $^ $(LDFLAGS)
	@echo "Built shared library: $@"

# Install to parent directory for easier linking
install: static shared
	cp $(BUILD_DIR)/$(STATIC_LIB) ../
	cp $(BUILD_DIR)/$(SHARED_LIB) ../
	@echo "Installed libraries to parent directory"

clean:
	rm -rf $(BUILD_DIR)
	rm -f ../$(STATIC_LIB) ../$(SHARED_LIB)
