# PSX DESR UDPFS XMB Game Installer
#
# Release build graph (make dist), strictly in this order:
#   1. build/opl-launcher/OPL-Launcher.elf        pinned upstream, unmodified
#   2. build/kelf/opl-launcher-EXECUTE.KELF       signed + verified
#   3. build/app/desr-udpfs-installer-app.elf     embeds (2)
#   4. build/kelf/installer-EXECUTE.KELF          (3) signed + verified
#   5. build/bootstrap/desr-udpfs-installer-bootstrap.elf   embeds (2) and (4)
#   6. dist/
# Signing needs kelftool and PS2KEYS=/absolute/path/to/PS2KEYS.dat.
#
#   make test           host unit tests (native cc; no PS2SDK, no keys)
#   make dev            unsigned development ELF (build/dev/), never in dist/
#   make kelfs          steps 1-4
#   make dist           steps 1-6 + test
#   make driver         rebuild vendor/irx/ps2hdd-hdl.irx from source (Docker)
#   make references     fetch pinned upstream sources into reference/
#
# Requires PS2DEV/PS2SDK (source tools/ps2env.sh) for everything but `test`.

ROOT := $(abspath .)
BUILD := $(ROOT)/build
DIST := $(ROOT)/dist
REF := $(ROOT)/reference

OPL_ELF := $(BUILD)/opl-launcher/OPL-Launcher.elf
OPL_KELF := $(BUILD)/kelf/opl-launcher-EXECUTE.KELF
APP_ELF := $(BUILD)/app/desr-udpfs-installer-app.elf
APP_KELF := $(BUILD)/kelf/installer-EXECUTE.KELF
LAUNCHER_ELF := $(BUILD)/launcher/app-launcher.elf
LAUNCHER_KELF := $(BUILD)/kelf/app-launcher-EXECUTE.KELF
BOOT_ELF := $(BUILD)/bootstrap/desr-udpfs-installer-bootstrap.elf
DEV_ELF := $(BUILD)/dev/desr-udpfs-installer-dev.elf
DRIVER := vendor/irx/ps2hdd-hdl.irx

NEUTRINO_IRX := $(BUILD)/irx/smap.irx $(BUILD)/irx/ministack.irx $(BUILD)/irx/udpfs_ioman.irx \
                $(BUILD)/irx/hddpump.irx
EE_DEPS := $(NEUTRINO_IRX) $(BUILD)/.driver-ok $(BUILD)/.refs-ok $(BUILD)/.gitid $(BUILD)/.sdk-ok

# KELF_MODE is part of every KELF's identity: switching it re-signs and
# re-embeds. dnasload (default; the header PSX XMB channels start), mbr
# and none are accepted by kelf-sign.sh.
KELF_MODE ?= dnasload
export KELF_MODE

# Stamps that change content (and so trigger rebuilds) only when their
# input changes. FORCE makes make re-evaluate them every run.
define UPDATE_STAMP
	@mkdir -p $(dir $(1)); printf '%s\n' "$(2)" > $(1).new; \
	 if cmp -s $(1).new $(1); then rm -f $(1).new; else mv -f $(1).new $(1); fi
endef

$(BUILD)/.kelf-mode: FORCE
	$(call UPDATE_STAMP,$@,$(KELF_MODE))

# Build id shown by the console diagnostics page.
$(BUILD)/.gitid: FORCE
	$(call UPDATE_STAMP,$@,$(shell git describe --always --dirty --abbrev=12 2>/dev/null))

# Upstream inputs must be exactly the pinned revisions.
$(BUILD)/.refs-ok: reference/REVISIONS.txt FORCE
	@bash tools/fetch-references.sh --check
	$(call UPDATE_STAMP,$@,$(shell cat reference/REVISIONS.txt))

# PS2SDK: the pinned build (tools/ps2sdk.env, `make ps2sdk`). Its
# ps2atad.irx must address the PS2 area of a DESR with LBA48-aware DVRP
# firmware (dvrpwned) with LBA48; the stamp records which SDK built it.
$(BUILD)/.sdk-ok: tools/ps2sdk.env FORCE
	@. tools/ps2sdk.env; grep -q "$$PS2SDK_ATAD_LBA48_MARK" "$(PS2SDK)/iop/irx/ps2atad.irx" || { \
	  echo "ERROR: $(PS2SDK)/iop/irx/ps2atad.irx has no LBA48 DVRP support."; \
	  echo "       Run 'make ps2sdk', then source tools/ps2env.sh again."; exit 1; }
	$(call UPDATE_STAMP,$@,$(PS2SDK) $(shell cat $(PS2SDK)/.psx-installer-rev 2>/dev/null))

ps2sdk:
	bash tools/build-ps2sdk.sh

.PHONY: all dev kelfs dist package test test-graph test-udpfsd udpfsd references irx driver driver-check ps2sdk clean distclean FORCE

all: test dev

references:
	bash tools/fetch-references.sh

test:
	$(MAKE) -C test/host
	python3 test/host/test_driver.py
	bash test/host/test_kelf_sign.sh
	python3 tools/verify-assets.py

# Release graph with a fake kelftool in a private directory (needs PS2SDK).
test-graph:
	bash test/host/test_build_graph.sh

# ---- udpfsd server (pinned upstream + patches/udpfsd: udpfsd.cfg, mounts, game prep)
# Needs Go >= 1.25 or Docker. `make test-udpfsd` runs only its Go tests.
UDPFSD_BIN := $(BUILD)/udpfsd/udpfsd-windows-amd64.exe $(BUILD)/udpfsd/udpfsd-linux-amd64

udpfsd: $(UDPFSD_BIN)
$(UDPFSD_BIN) &: $(BUILD)/.refs-ok $(wildcard patches/udpfsd/*.patch) tools/build-udpfsd.sh
	bash tools/build-udpfsd.sh $(BUILD)/udpfsd

test-udpfsd: $(BUILD)/.refs-ok
	bash tools/build-udpfsd.sh $(BUILD)/udpfsd test

# ---- OPL runtime handed out by udpfsd when the console has none ---------
# Pinned official build (tools/opl.env), verified by SHA-256.
OPL_RUNTIME := $(BUILD)/opl/OPNPS2LD.ELF
$(OPL_RUNTIME): tools/opl.env tools/fetch-opl.sh
	bash tools/fetch-opl.sh $(BUILD)/opl

# ---- HDD driver ---------------------------------------------------------
# vendor/irx/ps2hdd-hdl.irx is the output of `make driver` (reproducible
# legacy source build, see tools/driver/README.md). Every build checks
# its hash against tools/driver/driver.env.
driver:
	bash tools/driver/make-driver.sh

driver-check: $(BUILD)/.driver-ok
$(BUILD)/.driver-ok: $(DRIVER) reference/HDLGameInstaller/irx/hdlfs.irx tools/driver/driver.env
	@. tools/driver/driver.env; \
	 h=$$(sha256sum $(DRIVER) | cut -d' ' -f1); \
	 if [ "$$h" != "$$DRIVER_PATCHED_SHA256" ]; then \
	   echo "ERROR: $(DRIVER) sha256 $$h != $$DRIVER_PATCHED_SHA256 (tools/driver/driver.env)"; exit 1; fi; \
	 h=$$(sha256sum reference/HDLGameInstaller/irx/hdlfs.irx | cut -d' ' -f1); \
	 if [ "$$h" != "$$HDLFS_SHA256" ]; then \
	   echo "ERROR: hdlfs.irx sha256 $$h != $$HDLFS_SHA256 (tools/driver/driver.env)"; exit 1; fi
	@mkdir -p $(@D) && touch $@

# ---- Neutrino IOP modules (smap, ministack, udpfs_ioman) -------------
# Built from the pinned reference tree in a scratch copy with
# patches/neutrino/*.patch applied; reference/ is never modified.
irx: $(NEUTRINO_IRX)

$(BUILD)/neutrino/.patched: $(wildcard patches/neutrino/*.patch) $(BUILD)/.refs-ok \
                             src/dhcp_proto.c src/dhcp_proto.h $(BUILD)/.sdk-ok
	@test -d $(REF)/neutrino || { echo "reference/neutrino missing: run 'make references'"; exit 1; }
	rm -rf $(BUILD)/neutrino
	mkdir -p $(BUILD)/neutrino
	cp -r $(REF)/neutrino/iop $(REF)/neutrino/common $(BUILD)/neutrino/
	for p in $(sort $(wildcard patches/neutrino/*.patch)); do \
	  patch -d $(BUILD)/neutrino -p1 --forward < $$p || exit 1; done
	@# The DHCP client's message code is shared with the host tests.
	cp src/dhcp_proto.c $(BUILD)/neutrino/iop/ministack/src/
	cp src/dhcp_proto.h $(BUILD)/neutrino/iop/ministack/include/
	touch $@

$(BUILD)/irx/smap.irx: $(BUILD)/neutrino/.patched
	$(MAKE) -C $(BUILD)/neutrino/iop/smap all DEBUG=0
	@mkdir -p $(@D) && cp $(BUILD)/neutrino/iop/smap/irx/smap.irx $@

$(BUILD)/irx/ministack.irx: $(BUILD)/neutrino/.patched
	$(MAKE) -C $(BUILD)/neutrino/iop/ministack all DEBUG=0
	@mkdir -p $(@D) && cp $(BUILD)/neutrino/iop/ministack/irx/ministack.irx $@

# Our own IOP module (overlapped installs), built in a scratch copy.
$(BUILD)/irx/hddpump.irx: $(wildcard iop/hddpump/src/*) iop/hddpump/Makefile $(BUILD)/.sdk-ok
	rm -rf $(BUILD)/hddpump && mkdir -p $(BUILD)/hddpump && cp -r iop/hddpump/. $(BUILD)/hddpump/
	$(MAKE) -C $(BUILD)/hddpump
	@mkdir -p $(@D) && cp $(BUILD)/hddpump/irx/hddpump.irx $@

$(BUILD)/irx/udpfs_ioman.irx: $(BUILD)/neutrino/.patched
	$(MAKE) -C $(BUILD)/neutrino/iop/udpfs all DEBUG=0 UDPFS_IOMAN=1
	@mkdir -p $(@D) && cp $(BUILD)/neutrino/iop/udpfs/irx/udpfs_ioman.irx $@

# ---- 1. OPL-Launcher (pinned, unmodified) -----------------------------
# tools/bin2s stands in for the bin2s tool current PS2SDK no longer
# ships, so the upstream Makefile builds without patches. Upstream's
# EE_CFLAGS (-O2 -G8192 -mgpopt) is replaced on the command line: the
# current EE toolchain rejects -G with abicalls. -G only controls the
# small-data optimisation; code semantics are unchanged.
OPL_LAUNCHER_CFLAGS := -D_EE -G0 -O2 -Wno-stringop-truncation

$(OPL_ELF): $(BUILD)/.refs-ok tools/bin2s $(BUILD)/.sdk-ok
	@test -d $(REF)/OPL-Launcher || { echo "reference/OPL-Launcher missing: run 'make references'"; exit 1; }
	rm -rf $(BUILD)/opl-launcher
	mkdir -p $(BUILD)/opl-launcher
	cp -r $(REF)/OPL-Launcher/. $(BUILD)/opl-launcher/
	rm -rf $(BUILD)/opl-launcher/.git
	chmod +x tools/bin2s
	PATH="$(ROOT)/tools:$$PATH" $(MAKE) -C $(BUILD)/opl-launcher \
	  EE_CFLAGS="$(OPL_LAUNCHER_CFLAGS)"

# ---- 2. signed OPL-Launcher -------------------------------------------
# Signed without its debug sections (1.5 MB -> ~0.3 MB), like the app.
OPL_ELF_STRIPPED := $(BUILD)/opl-launcher/OPL-Launcher-stripped.elf
$(OPL_ELF_STRIPPED): $(OPL_ELF)
	mips64r5900el-ps2-elf-strip -o $@ $<

$(OPL_KELF): $(OPL_ELF_STRIPPED) tools/kelf-sign.sh $(BUILD)/.kelf-mode
	bash tools/kelf-sign.sh $(OPL_ELF_STRIPPED) $@

# ---- 2b. app launcher (launcher/main.c), signed -----------------------
# The EXECUTE.KELF of every app channel: runs the ELF its APP.CFG names.
$(LAUNCHER_ELF): $(BUILD)/.sdk-ok FORCE
	$(MAKE) -f Makefile.launcher BUILD=$(BUILD)/launcher EE_BIN=$(BUILD)/launcher/app-launcher-debug.elf
	$(call STRIP_IF_CHANGED,$@,$(BUILD)/launcher/app-launcher-debug.elf)

$(LAUNCHER_KELF): $(LAUNCHER_ELF) tools/kelf-sign.sh $(BUILD)/.kelf-mode
	bash tools/kelf-sign.sh $(LAUNCHER_ELF) $@

# ---- 3. app ELF (embeds 2) ---------------------------------------------
# The sub-make is always entered (FORCE) but only relinks when an input
# changed. The release ELF is the debug-stripped link output and is only
# replaced when its bytes change, so step 4 re-signs only a changed ELF.
EE_STRIP_CMD = mips64r5900el-ps2-elf-strip
define STRIP_IF_CHANGED
	$(EE_STRIP_CMD) -o $(1).new $(2)
	if cmp -s $(1).new $(1); then rm -f $(1).new; else mv -f $(1).new $(1); fi
endef

$(APP_ELF): $(OPL_KELF) $(LAUNCHER_KELF) $(EE_DEPS) FORCE
	$(MAKE) -f Makefile.ee VARIANT=app BUILD=$(BUILD)/app EE_BIN=$(BUILD)/app/app-debug.elf \
	  IRX_DIR=$(BUILD)/irx GITID_STAMP=$(BUILD)/.gitid \
	  EMBED_KELFS="opl_launcher_kelf=$(OPL_KELF) app_launcher_kelf=$(LAUNCHER_KELF)"
	$(call STRIP_IF_CHANGED,$@,$(BUILD)/app/app-debug.elf)

# ---- 4. signed app -----------------------------------------------------
$(APP_KELF): $(APP_ELF) tools/kelf-sign.sh $(BUILD)/.kelf-mode
	bash tools/kelf-sign.sh $(APP_ELF) $@

# ---- 5. bootstrap ELF (embeds 2 and 4) --------------------------------
$(BOOT_ELF): $(OPL_KELF) $(LAUNCHER_KELF) $(APP_KELF) $(EE_DEPS) FORCE
	$(MAKE) -f Makefile.ee VARIANT=bootstrap BUILD=$(BUILD)/bootstrap \
	  EE_BIN=$(BUILD)/bootstrap/bootstrap-debug.elf IRX_DIR=$(BUILD)/irx \
	  GITID_STAMP=$(BUILD)/.gitid \
	  EMBED_KELFS="opl_launcher_kelf=$(OPL_KELF) installer_kelf=$(APP_KELF) app_launcher_kelf=$(LAUNCHER_KELF)"
	$(call STRIP_IF_CHANGED,$@,$(BUILD)/bootstrap/bootstrap-debug.elf)

# ---- POPStarter for PS1 games, shipped in udpfsd/POPS/: the pinned
# rev13 Beta POPSTARTER.KELF as distributed (tools/popstarter.env, kept in
# vendor/popstarter/), the one PFS-BatchKit-Manager and PSX-XMB-Manager
# use on the PSX. POPSTARTER_ELF=/path/POPSTARTER.ELF signs another one
# like the other KELFs instead. POPS.ELF / IOPRP252.IMG are never included.
POPS_KELF := $(BUILD)/kelf/POPSTARTER.KELF
POPSTARTER_PIN := vendor/popstarter/POPSTARTER.KELF
ifneq ($(POPSTARTER_ELF),)
$(POPS_KELF): $(POPSTARTER_ELF) tools/kelf-sign.sh $(BUILD)/.kelf-mode
	bash tools/kelf-sign.sh $(POPSTARTER_ELF) $@
else
$(POPS_KELF): tools/popstarter.env tools/fetch-popstarter.sh $(POPSTARTER_PIN)
	bash tools/fetch-popstarter.sh $@
endif
DIST_POPS := $(POPS_KELF)

kelfs: $(OPL_KELF) $(LAUNCHER_KELF) $(APP_KELF)

# ---- dev (unsigned, nothing embedded) --------------------------------
dev: $(EE_DEPS) FORCE
	$(MAKE) -f Makefile.ee VARIANT=dev BUILD=$(BUILD)/dev EE_BIN=$(DEV_ELF) \
	  IRX_DIR=$(BUILD)/irx GITID_STAMP=$(BUILD)/.gitid

# ---- 6. dist -----------------------------------------------------------
DIST_FILES := desr-udpfs-installer-bootstrap.elf desr-udpfs-installer-app.elf \
              installer-EXECUTE.KELF opl-launcher-EXECUTE.KELF

dist: test $(BOOT_ELF) $(UDPFSD_BIN) $(OPL_RUNTIME) $(DIST_POPS)
	@# Empty dist/ rather than delete it: an Explorer window open on a
	@# folder in it locks the folder on Windows. No old file may survive,
	@# except udpfsd's own state when it is run from dist/udpfsd: its
	@# cache (prepared covers + manifest it is serving) and a running
	@# udpfsd (Windows locks it) that is byte-identical to the new one.
	mkdir -p $(DIST)
	find $(DIST) -mindepth 1 -depth -not -path '$(DIST)/udpfsd/udpfsd-cache*' \
	  -not -path '$(DIST)/udpfsd' -delete 2>/dev/null || true
	@for f in $$(find $(DIST) -type f -not -path '$(DIST)/udpfsd/udpfsd-cache/*'); do \
	  cmp -s "$$f" "$(BUILD)/udpfsd/$$(basename "$$f")" || \
	    { echo "dist: cannot replace $$f (stop udpfsd first)"; exit 1; }; \
	done
	mkdir -p $(DIST)/udpfsd-example $(DIST)/docs $(DIST)/udpfsd $(DIST)/udpfsd/POPS
	$(if $(DIST_POPS),cp $(DIST_POPS) $(DIST)/udpfsd/POPS/)
	cp $(BOOT_ELF) $(APP_ELF) $(APP_KELF) $(OPL_KELF) $(DIST)/
	for b in $(UDPFSD_BIN); do \
	  cmp -s "$$b" $(DIST)/udpfsd/$$(basename "$$b") || cp "$$b" $(DIST)/udpfsd/; \
	done
	cp $(OPL_KELF) docs/udpfsd-example/udpfsd.cfg $(DIST)/udpfsd/
	cp $(OPL_RUNTIME) $(BUILD)/opl/OPL-LICENSE.txt $(DIST)/udpfsd/
	cp docs/udpfsd-example/* $(DIST)/udpfsd-example/
	cp docs/INSTALL.md docs/HARDWARE_TEST_CHECKLIST.md $(DIST)/docs/
	cp docs/QUICKSTART.md $(DIST)/
	cp KNOWN_LIMITATIONS.md CHANGELOG.md docs/SERVER_MANUAL.md $(DIST)/
	bash tools/write-manifest.sh $(DIST) $(DRIVER) $(BUILD)/irx $(OPL_ELF) $(BUILD)/.kelf-mode

# ---- 7. end-user zip (README, PS2 bootstrap ELF, PC/udpfsd folder) -----
VERSION := 3.1
PACKAGE := $(ROOT)/PSX-UDPFS-Installer_V$(VERSION).zip
package: dist
	bash tools/make-package.sh $(DIST) $(PACKAGE)

clean:
	rm -rf $(BUILD)
	rm -f $(DIST)/*.tmp
	find . -name '*.KELF.tmp' -o -name '*.KELF.verify.elf' | xargs -r rm -f
	$(MAKE) -C test/host clean

distclean: clean
	rm -rf $(DIST)

FORCE:
