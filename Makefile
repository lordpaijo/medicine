CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
PORT    ?= 8080
SITE    ?= test/site

SRC_DIR   := src
BUILD_DIR := build
TARGET    := $(BUILD_DIR)/mdserve

MODULE_DIRS := $(wildcard $(SRC_DIR)/modules/*)
SRCS := $(SRC_DIR)/main.c $(wildcard $(SRC_DIR)/modules/*/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

CPPFLAGS += $(addprefix -I,$(MODULE_DIRS)) -MMD -MP

ifeq ($(V),1)
  Q :=
else
  Q := @
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "  LINK  $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	$(Q)$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET) -p $(PORT) $(SITE)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

.PHONY: all run clean
