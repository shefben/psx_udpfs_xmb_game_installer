# Host-side unit tests. Builds the platform-neutral modules from src/
# with the native compiler and runs them. No PS2SDK needed.

CC ?= cc
CFLAGS ?= -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined
SRC := ../../src
BUILD := build

# Platform-neutral modules (no PS2SDK headers outside #ifdef _EE).
LIB_SRCS := errors.c util.c partname.c source.c iso9660.c hdl_plan.c \
            xmb_text.c apa_osd_header.c transaction.c settings.c \
            opl_dependency.c hdl_header.c game_pair.c crc32.c sha256.c batch.c \
            manifest.c server_assets.c ui_style.c rw_buffer.c \
            remove_games.c listview.c lz4_block.c source_zso.c
CFLAGS += -DFIXTURE_DIR=\"$(abspath ../fixtures)\"
TEST_SRCS := $(wildcard test_*.c)

OBJS := $(addprefix $(BUILD)/lib_,$(LIB_SRCS:.c=.o)) \
        $(addprefix $(BUILD)/,$(TEST_SRCS:.c=.o))

# Reference implementations extracted verbatim from pinned upstream
# sources, for parity tests. Skipped if reference/ was not fetched.
HDLDUMP_HDL_C := ../../reference/hdl-dump/hdl.c
ifneq ($(wildcard $(HDLDUMP_HDL_C)),)
CFLAGS += -DHAVE_HDLDUMP_REF
OBJS += $(BUILD)/ref_hdl_pname.o
endif

all: run

# Pull `void hdl_pname(...) { ... }` out of hdl.c (ends at the first
# column-0 closing brace).
$(BUILD)/ref_hdl_pname.c: $(HDLDUMP_HDL_C)
	@mkdir -p $(BUILD)
	{ printf '#include <ctype.h>\n#include <string.h>\n#include <sys/types.h>\n#define PS2_PART_IDMAX 32\n'; \
	  awk '/^void hdl_pname\(/{p=1} p{print} p&&/^}/{exit}' $< ; } > $@

$(BUILD)/ref_hdl_pname.o: $(BUILD)/ref_hdl_pname.c
	$(CC) $(CFLAGS) -w -c -o $@ $<

$(BUILD)/lib_%.o: $(SRC)/%.c $(wildcard $(SRC)/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -c -o $@ $<

$(BUILD)/%.o: %.c test.h $(wildcard $(SRC)/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(SRC) -c -o $@ $<

$(BUILD)/tests: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

run: $(BUILD)/tests
	$(BUILD)/tests

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
