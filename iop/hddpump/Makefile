# hddpump.irx: IOP-side HDD writer for overlapped installs (see src/main.c).
IOP_SRC_DIR  = src/
IOP_INC_DIR  = src/
IOP_OBJS_DIR = obj/
IOP_BIN      = irx/hddpump.irx
IOP_OBJS     = main.o imports.o

all: $(IOP_BIN)

clean:
	rm -rf $(IOP_OBJS_DIR) irx

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal

$(IOP_BIN): | irx
irx:
	mkdir -p irx
