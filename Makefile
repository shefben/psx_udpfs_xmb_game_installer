# PSX DESR UDPFS XMB Game Installer
#
#   make all            installer ELF + OPL-Launcher ELF + host tests
#   make installer      dist/desr-udpfs-installer.elf
#   make opl-launcher   build/opl-launcher/OPL-Launcher.elf (unsigned)
#   make kelfs          dist/installer-EXECUTE.KELF, dist/opl-launcher-EXECUTE.KELF
#                       (needs kelftool + your PS2KEYS.dat; see docs/BUILD.md)
#   make test           host unit tests (native cc, no PS2SDK needed)
#   make dist           everything for first bootstrap and server setup
#   make references     fetch pinned upstream sources into reference/
#
# Requires PS2DEV/PS2SDK (source tools/ps2env.sh) for everything but `test`.

ROOT := $(abspath .)
BUILD := $(ROOT)/build
DIST := $(ROOT)/dist
REF := $(ROOT)/reference

INSTALLER_ELF := $(DIST)/desr-udpfs-installer.elf
OPL_LAUNCHER_ELF := $(BUILD)/opl-launcher/OPL-Launcher.elf
OPL_LAUNCHER_KELF_VENDOR := $(ROOT)/vendor/opl-launcher/EXECUTE.KELF

NEUTRINO_IRX := $(BUILD)/irx/smap.irx $(BUILD)/irx/ministack.irx $(BUILD)/irx/udpfs_ioman.irx

.PHONY: all installer opl-launcher kelfs test dist references irx clean distclean

all: installer opl-launcher test

references:
	bash tools/fetch-references.sh

test:
	$(MAKE) -C test/host

# ---- Neutrino IOP modules (smap, ministack, udpfs_ioman) -------------
# Built from the pinned reference tree in a scratch copy with
# patches/neutrino/*.patch applied; reference/ is never modified.
irx: $(NEUTRINO_IRX)

$(BUILD)/neutrino/.patched: $(wildcard patches/neutrino/*.patch)
	@test -d $(REF)/neutrino || { echo "reference/neutrino missing: run 'make references'"; exit 1; }
	rm -rf $(BUILD)/neutrino
	mkdir -p $(BUILD)/neutrino
	cp -r $(REF)/neutrino/iop $(REF)/neutrino/common $(BUILD)/neutrino/
	for p in $(sort $(wildcard patches/neutrino/*.patch)); do \
	  patch -d $(BUILD)/neutrino -p1 --forward < $$p || exit 1; done
	touch $@

$(BUILD)/irx/smap.irx: $(BUILD)/neutrino/.patched
	$(MAKE) -C $(BUILD)/neutrino/iop/smap all DEBUG=0
	@mkdir -p $(@D) && cp $(BUILD)/neutrino/iop/smap/irx/smap.irx $@

$(BUILD)/irx/ministack.irx: $(BUILD)/neutrino/.patched
	$(MAKE) -C $(BUILD)/neutrino/iop/ministack all DEBUG=0
	@mkdir -p $(@D) && cp $(BUILD)/neutrino/iop/ministack/irx/ministack.irx $@

$(BUILD)/irx/udpfs_ioman.irx: $(BUILD)/neutrino/.patched
	$(MAKE) -C $(BUILD)/neutrino/iop/udpfs all DEBUG=0 UDPFS_IOMAN=1
	@mkdir -p $(@D) && cp $(BUILD)/neutrino/iop/udpfs/irx/udpfs_ioman.irx $@

# ---- Installer ELF ----------------------------------------------------
installer: $(NEUTRINO_IRX)
	$(MAKE) -f Makefile.ee BUILD=$(BUILD)/ee EE_BIN=$(INSTALLER_ELF) \
	  NEUTRINO_IRX_DIR=$(BUILD)/irx OPL_LAUNCHER_KELF=$(wildcard $(OPL_LAUNCHER_KELF_VENDOR))

# ---- OPL-Launcher (pinned, unmodified) --------------------------------
# tools/bin2s stands in for the bin2s tool current PS2SDK no longer
# ships, so the upstream Makefile builds without patches. Upstream's
# EE_CFLAGS (-O2 -G8192 -mgpopt) is replaced on the command line: the
# current EE toolchain rejects -G with abicalls. -G only controls the
# small-data optimisation; code semantics are unchanged.
OPL_LAUNCHER_CFLAGS := -D_EE -G0 -O2 -Wno-stringop-truncation
opl-launcher: $(OPL_LAUNCHER_ELF)

$(OPL_LAUNCHER_ELF):
	@test -d $(REF)/OPL-Launcher || { echo "reference/OPL-Launcher missing: run 'make references'"; exit 1; }
	rm -rf $(BUILD)/opl-launcher
	mkdir -p $(BUILD)/opl-launcher
	cp -r $(REF)/OPL-Launcher/. $(BUILD)/opl-launcher/
	rm -rf $(BUILD)/opl-launcher/.git
	chmod +x tools/bin2s
	PATH="$(ROOT)/tools:$$PATH" $(MAKE) -C $(BUILD)/opl-launcher \
	  EE_CFLAGS="$(OPL_LAUNCHER_CFLAGS)"

# ---- Signed payloads ---------------------------------------------------
kelfs: installer opl-launcher
	bash tools/package-installer-kelf.sh $(INSTALLER_ELF) $(DIST)/installer-EXECUTE.KELF
	bash tools/package-opl-launcher.sh $(OPL_LAUNCHER_ELF) $(DIST)/opl-launcher-EXECUTE.KELF

# ---- Distribution ------------------------------------------------------
dist: installer opl-launcher test
	mkdir -p $(DIST)/udpfsd-example $(DIST)/PAYLOAD
	cp docs/udpfsd-example/* $(DIST)/udpfsd-example/
	cp docs/INSTALL.md $(DIST)/INSTALL.md
	cp KNOWN_LIMITATIONS.md $(DIST)/
	cp $(OPL_LAUNCHER_ELF) $(DIST)/opl-launcher-unsigned.elf
	@if [ -f $(DIST)/installer-EXECUTE.KELF ] && [ -f $(DIST)/opl-launcher-EXECUTE.KELF ]; then \
	  cp $(DIST)/installer-EXECUTE.KELF $(DIST)/opl-launcher-EXECUTE.KELF $(DIST)/PAYLOAD/; \
	  echo "dist: signed KELFs staged in dist/PAYLOAD/"; \
	else \
	  echo "dist: NOTE signed KELFs not present - run 'make kelfs' (needs kelftool + PS2KEYS.dat)"; \
	fi
	cd $(DIST) && sha256sum desr-udpfs-installer.elf opl-launcher-unsigned.elf \
	  $$(ls installer-EXECUTE.KELF opl-launcher-EXECUTE.KELF 2>/dev/null) > SHA256SUMS

clean:
	rm -rf $(BUILD)
	$(MAKE) -C test/host clean

distclean: clean
	rm -rf $(DIST)
