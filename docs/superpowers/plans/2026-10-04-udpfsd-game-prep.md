# udpfsd Game Preparation, Launcher Transfer and Auto-Install — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** udpfsd reads its folders from `udpfsd.cfg`, prepares every ISO/ZSO (ID, title, jacket, OPL cfg, metadata) into `udpfs:/.udpfsd/`, serves the OPL-Launcher KELF, and the PS2 app uses all of it — up to a fully automatic run from "ELF started" to "games bootable from the XMB".

**Architecture:** Server side is a second udpfsd patch (`patches/udpfsd/0002-game-prep.patch`, applied after 0001) adding `internal/config`, a generalized read-only mount table, and `internal/prep` (ISO probe, titles, art, downloads, scanner, manifest). PS2 side adds a pure manifest parser, server-asset helpers, manifest-driven batch/auto flows, OPL cfg copy, and server-launcher selection; the install/verify/journal core is unchanged.

**Tech Stack:** Go 1.25 (stdlib only: `image/png`, `image/jpeg`, `image/gif`, `net/http`, `crypto/sha256`), C (PS2SDK EE, gnu11), host tests with cc + ASan/UBSan, Go tests in the digest-pinned `golang:1.25` Docker image.

**Spec:** `docs/superpowers/specs/2026-10-04-udpfsd-game-prep-design.md`

## Global Constraints

- Config file `udpfsd.cfg` in the directory of the udpfsd binary; `-config <file>` overrides; unknown keys are a startup error naming the line; relative paths resolve against the binary's directory.
- Keys: `dvd cd games install cfg art gamelist cache download_covers opl_launcher auto_install fsroot read_only port bind`.
- Precedence: command-line flag > environment variable > `udpfsd.cfg` > built-in default. Existing flags keep working.
- Mounts (always read-only to the console): `/DVD /CD /GAMES /INSTALL /ART /CFG /.udpfsd`; a same-named real fsroot folder is hidden.
- Game ID always from the disc's `SYSTEM.CNF`.
- Title order: `cfg\<ID>.cfg` `Title=` > `gamelist` > file name without `.iso`/`.zso` and trailing `(...)`/`[...]` groups.
- Jacket order: `art\<ID>_COV.(png|jpg)`, `art\<ID>_COV2.*`, `art\<ID>.*`, image named like the game file next to it, download, else none; output 74x108 PNG `jkt/<ID>.png`.
- Download URL: `https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default/<XXXX-NNNNN>.jpg`, 10 s timeout, at most 4 in parallel; failures never delay serving.
- Manifest: first line `udpfsd-manifest 1 [launcher=<sha256>:<bytes>] [auto=0|1]`; entries tab-separated `path status id title bytes disc layer1 jacket cfg`; written atomically.
- Cache: `cache/scan.json` keyed by path + size + mtime.
- PS2: install core unchanged (re-probe, refuse if ID or size differ, copy, CRC read-back, journal, channel order).
- OPL cfg: copied to `<OPL partition>/CFG/<ID>.cfg` (`+` partitions) or `<OPL partition>/OPL/CFG/<ID>.cfg`, only if absent, `.tmp` + read-back + rename, best effort, journal `opl_cfg=copied|kept|failed|none`.
- Launcher: server copy used only if present, `kelf_looks_valid()`, and SHA-256/size equal the manifest header; else embedded. Journal records the source.
- Auto mode: `auto_install = yes`; 10 s cancellable countdown; first run creates `PP.UDPFS-INSTALLER`; OPL missing -> install nothing, stay in menu; only *new* games that fit free space; never delete/repair/overwrite; 15 s summary then exit to the system menu.

## Review Focus

1. **Manifest written while the PS2 reads it / scan still running** — the PS2 must never see a half-written manifest and auto mode must wait (bounded) instead of acting on "no games". Pinned by: Task 6 atomic-write test, Task 12 wait loop.
2. **Same game twice on the server** (user's `DVD\` has GTA SA as `.iso` and `.zso`) — exactly one install; the other is `duplicate`. Pinned by: Task 10 manifest-to-batch test.
3. **Titles with tabs/newlines/`^!`/very long names in GameListPS2 or CFG** — manifest must stay one line per game, titles ≤ 63 bytes at a UTF-8 boundary. Pinned by: Task 4 and Task 6 tests.
4. **Server copy of the launcher is stale or corrupt** — must fall back to embedded, never write a bad KELF into a channel. Pinned by: Task 9 test.
5. **Existing OPL cfg on the HDD** — must never be overwritten. Pinned by: Task 11 decision test.

---

## File Structure

Server (inside the udpfsd source tree, delivered as `patches/udpfsd/0002-game-prep.patch`):

| File | Responsibility |
|---|---|
| `internal/config/config.go` (+ `_test.go`) | parse `udpfsd.cfg`, precedence helpers |
| `internal/fs/mounts.go` (replaces `installdir.go`) (+ `mounts_test.go` replacing `installdir_test.go`) | read-only mount table |
| `internal/prep/iso.go` (+ test) | ISO9660/SYSTEM.CNF probe, logical reader for ISO/ZSO |
| `internal/prep/titles.go` (+ test) | gamelist, CFG title, file-name title |
| `internal/prep/art.go` (+ test) | art lookup, 74x108 PNG jacket |
| `internal/prep/download.go` (+ test) | cover downloads |
| `internal/prep/scan.go` (+ test) | scan, cache, manifest, launcher copy |
| `cmd/udpfsd/main.go` | config loading, mounts, background prep |

Project:

| File | Responsibility |
|---|---|
| `tools/udpfsd-patch.sh` | dev tree for writing/saving udpfsd patches, Go tests |
| `tools/build-udpfsd.sh` | test all packages; build; ship cfg template + launcher |
| `docs/udpfsd-example/udpfsd.cfg` | commented config template |
| `src/manifest.c/.h` (+ `test/host/test_manifest.c`) | pure manifest parser + lookups; EE loader |
| `src/server_assets.c/.h` (+ `test/host/test_server_assets.c`) | launcher validation, OPL cfg decision/paths |
| `src/transaction.c/.h` | journal fields `launcher_source`, `opl_cfg` |
| `src/batch.c/.h` | `BATCH_NO_SPACE`, `batch_auto_select()` |
| `src/opl_launcher_payload.c`, `src/xmb_game_channel.c/.h`, `src/flows.c/.h`, `src/browser.c`, `src/network.c`, `src/main.c`, `src/diagnostics.c` | integration |

---

### Task 1: udpfsd patch workflow and `internal/config`

**Files:**
- Create: `tools/udpfsd-patch.sh`
- Create (in dev tree): `internal/config/config.go`, `internal/config/config_test.go`
- Modify: `tools/build-udpfsd.sh` (test all packages except chd)

**Interfaces:**
- Produces: `config.Config` struct; `config.Defaults() Config`; `config.Load(path string, baseDir string) (Config, error)`; `(*Config).Set(key, value, baseDir string) error`; `config.Keys []string`.

- [ ] **Step 1: Create the dev-tree helper**

`tools/udpfsd-patch.sh`:

```bash
#!/usr/bin/env bash
# Work on udpfsd patches in build/udpfsd-dev.
#   init <N>     pinned reference + patches numbered < N, committed as base
#   test         go vet + go test (all packages except chd) in the dev tree
#   save <file>  write the dev tree's diff against base to patches/udpfsd/<file>
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEV=$ROOT/build/udpfsd-dev
GO_IMAGE=golang:1.25@sha256:699337d620559a59b4a2bb298ad59611e535d2ee755a34cf2d2a98f37578dc80
run_go() {
  if command -v go >/dev/null 2>&1; then (cd "$DEV" && sh -c "$1")
  elif command -v docker >/dev/null 2>&1; then docker run --rm -v "$DEV:/src" -w /src "$GO_IMAGE" sh -c "$1"
  else docker.exe run --rm -v "$(wslpath -w "$DEV"):/src" -w /src "$GO_IMAGE" sh -c "$1"; fi
}
case "$1" in
  init)
    rm -rf "$DEV"; mkdir -p "$DEV"
    git -C "$ROOT/reference/udpfsd" archive HEAD | tar -x -C "$DEV"
    for p in $(ls "$ROOT"/patches/udpfsd/*.patch 2>/dev/null | sort); do
      n=$(basename "$p" | cut -c1-4)
      [ "$((10#$n))" -lt "$((10#$2))" ] && patch -d "$DEV" -p1 --no-backup-if-mismatch < "$p" >/dev/null
    done
    (cd "$DEV" && git init -q && git add -A && git -c user.name=base -c user.email=base@local commit -qm base)
    echo "dev tree ready: $DEV" ;;
  test)
    run_go 'export CGO_ENABLED=0 GOFLAGS=-buildvcs=false; P=$(go list ./... | grep -v /chd); gofmt -l . | grep -v "^$" && exit 1; go vet $P && go test $P' ;;
  save)
    (cd "$DEV" && find . -name '*.go' -newer .git/HEAD -exec sed -i 's/\r$//' {} + && git add -A && git diff --cached > "$ROOT/patches/udpfsd/$2")
    echo "wrote patches/udpfsd/$2 ($(grep -c '^+++' "$ROOT/patches/udpfsd/$2") files)" ;;
  *) echo "usage: $0 init <N>|test|save <file>"; exit 2 ;;
esac
```

Run: `bash tools/udpfsd-patch.sh init 2`
Expected: `dev tree ready: .../build/udpfsd-dev` (pinned udpfsd + 0001 applied).

- [ ] **Step 2: Write the failing config test** — `build/udpfsd-dev/internal/config/config_test.go`

```go
package config

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func write(t *testing.T, dir, name, body string) string {
	t.Helper()
	p := filepath.Join(dir, name)
	if err := os.WriteFile(p, []byte(body), 0644); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestLoadFullFile(t *testing.T) {
	base := t.TempDir()
	for _, d := range []string{"DVD", "CD", "ART", "CFG"} {
		os.Mkdir(filepath.Join(base, d), 0755)
	}
	write(t, base, "GameListPS2.txt", "x\n")
	p := write(t, base, "udpfsd.cfg", strings.Join([]string{
		"# comment",
		"dvd = DVD",
		"cd=CD   # trailing comment",
		"art = " + filepath.Join(base, "ART"),
		"cfg = CFG",
		"gamelist = GameListPS2.txt",
		"download_covers = no",
		"auto_install = yes",
		"read_only = true",
		"port = 1234",
		"bind = 10.0.0.5",
		"",
	}, "\r\n"))
	c, err := Load(p, base)
	if err != nil {
		t.Fatal(err)
	}
	if c.DVD != filepath.Join(base, "DVD") || c.CD != filepath.Join(base, "CD") {
		t.Fatalf("relative dirs not resolved: %+v", c)
	}
	if c.ART != filepath.Join(base, "ART") || c.CFG != filepath.Join(base, "CFG") {
		t.Fatalf("art/cfg: %+v", c)
	}
	if c.Gamelist != filepath.Join(base, "GameListPS2.txt") {
		t.Fatalf("gamelist: %q", c.Gamelist)
	}
	if c.DownloadCovers || !c.AutoInstall || !c.ReadOnly || c.Port != 1234 || c.Bind != "10.0.0.5" {
		t.Fatalf("scalars: %+v", c)
	}
	if c.Cache != filepath.Join(base, "udpfsd-cache") {
		t.Fatalf("default cache: %q", c.Cache)
	}
}

func TestDefaults(t *testing.T) {
	c := Defaults()
	if !c.DownloadCovers || c.AutoInstall || c.Port != 62966 || c.Cache != "udpfsd-cache" {
		t.Fatalf("%+v", c)
	}
}

func TestUnknownKeyNamesLine(t *testing.T) {
	base := t.TempDir()
	p := write(t, base, "udpfsd.cfg", "port = 1\ndvdd = x\n")
	_, err := Load(p, base)
	if err == nil || !strings.Contains(err.Error(), "line 2") || !strings.Contains(err.Error(), "dvdd") {
		t.Fatalf("err = %v", err)
	}
}

func TestMissingFolderIsError(t *testing.T) {
	base := t.TempDir()
	p := write(t, base, "udpfsd.cfg", "dvd = nope\n")
	if _, err := Load(p, base); err == nil || !strings.Contains(err.Error(), "line 1") {
		t.Fatalf("err = %v", err)
	}
	p = write(t, base, "udpfsd.cfg", "gamelist = missing.txt\n")
	if _, err := Load(p, base); err == nil {
		t.Fatal("missing gamelist accepted")
	}
}

func TestBadValues(t *testing.T) {
	base := t.TempDir()
	for _, body := range []string{"port = abc\n", "auto_install = maybe\n", "novalue\n"} {
		p := write(t, base, "udpfsd.cfg", body)
		if _, err := Load(p, base); err == nil {
			t.Fatalf("accepted %q", body)
		}
	}
}

func TestSetUsedForOverrides(t *testing.T) {
	base := t.TempDir()
	os.Mkdir(filepath.Join(base, "q"), 0755)
	c := Defaults()
	if err := c.Set("install", "q", base); err != nil || c.Install != filepath.Join(base, "q") {
		t.Fatalf("%v %q", err, c.Install)
	}
	if err := c.Set("read_only", "1", base); err != nil || !c.ReadOnly {
		t.Fatal("read_only")
	}
}

func TestHasGameFolders(t *testing.T) {
	c := Defaults()
	if c.HasGameFolders() {
		t.Fatal("empty config has folders")
	}
	c.Games = "/x"
	if !c.HasGameFolders() {
		t.Fatal("games folder not detected")
	}
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `bash tools/udpfsd-patch.sh test`
Expected: FAIL — `undefined: Load` / `undefined: Defaults` (package has no implementation).

- [ ] **Step 4: Implement** — `build/udpfsd-dev/internal/config/config.go`

```go
// Package config reads udpfsd.cfg: "key = value" lines, '#' comments.
package config

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Config holds every udpfsd.cfg setting. Paths are absolute after Load/Set.
type Config struct {
	DVD, CD, Games, Install, CFG, ART string
	Gamelist, Cache, OPLLauncher, FSRoot string
	DownloadCovers, AutoInstall, ReadOnly  bool
	Port                                   int
	Bind                                   string
	// OPLLauncherSet reports that opl_launcher was given explicitly
	// (then a missing file is an error; the default is optional).
	OPLLauncherSet bool
}

// Keys lists every accepted key.
var Keys = []string{"dvd", "cd", "games", "install", "cfg", "art", "gamelist", "cache",
	"download_covers", "opl_launcher", "auto_install", "fsroot", "read_only", "port", "bind"}

// Defaults returns the built-in defaults (relative paths not yet resolved).
func Defaults() Config {
	return Config{Cache: "udpfsd-cache", OPLLauncher: "opl-launcher-EXECUTE.KELF",
		DownloadCovers: true, Port: 62966}
}

// HasGameFolders reports whether any game folder is configured.
func (c *Config) HasGameFolders() bool {
	return c.DVD != "" || c.CD != "" || c.Games != "" || c.Install != ""
}

func resolve(v, baseDir string) string {
	if v == "" || filepath.IsAbs(v) || (len(v) > 1 && v[1] == ':') {
		return v
	}
	return filepath.Join(baseDir, v)
}

func parseBool(v string) (bool, error) {
	switch strings.ToLower(v) {
	case "yes", "true", "1", "on":
		return true, nil
	case "no", "false", "0", "off":
		return false, nil
	}
	return false, fmt.Errorf("%q is not yes/no", v)
}

func mustDir(p string) error {
	if st, err := os.Stat(p); err != nil || !st.IsDir() {
		return fmt.Errorf("%s is not a directory", p)
	}
	return nil
}

// Set applies one key (used by Load and for flag/env overrides).
func (c *Config) Set(key, value, baseDir string) error {
	v := strings.TrimSpace(value)
	dir := func(dst *string) error {
		*dst = resolve(v, baseDir)
		if *dst == "" {
			return nil
		}
		return mustDir(*dst)
	}
	var err error
	switch key {
	case "dvd":
		return dir(&c.DVD)
	case "cd":
		return dir(&c.CD)
	case "games":
		return dir(&c.Games)
	case "install":
		return dir(&c.Install)
	case "cfg":
		return dir(&c.CFG)
	case "art":
		return dir(&c.ART)
	case "fsroot":
		return dir(&c.FSRoot)
	case "gamelist":
		c.Gamelist = resolve(v, baseDir)
		if c.Gamelist != "" {
			if _, err := os.Stat(c.Gamelist); err != nil {
				return fmt.Errorf("gamelist %s: %w", c.Gamelist, err)
			}
		}
	case "cache":
		c.Cache = resolve(v, baseDir)
	case "opl_launcher":
		c.OPLLauncher = resolve(v, baseDir)
		c.OPLLauncherSet = true
		if _, err := os.Stat(c.OPLLauncher); err != nil {
			return fmt.Errorf("opl_launcher %s: %w", c.OPLLauncher, err)
		}
	case "download_covers":
		c.DownloadCovers, err = parseBool(v)
	case "auto_install":
		c.AutoInstall, err = parseBool(v)
	case "read_only":
		c.ReadOnly, err = parseBool(v)
	case "port":
		c.Port, err = strconv.Atoi(v)
	case "bind":
		c.Bind = v
	default:
		return fmt.Errorf("unknown key %q", key)
	}
	return err
}

// Load reads path; relative values resolve against baseDir.
func Load(path, baseDir string) (Config, error) {
	c := Defaults()
	c.Cache = resolve(c.Cache, baseDir)
	c.OPLLauncher = resolve(c.OPLLauncher, baseDir)
	f, err := os.Open(path)
	if err != nil {
		return c, err
	}
	defer f.Close()
	sc := bufio.NewScanner(f)
	for n := 1; sc.Scan(); n++ {
		line := sc.Text()
		if i := strings.Index(line, "#"); i >= 0 {
			line = line[:i]
		}
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			return c, fmt.Errorf("%s line %d: expected key = value", path, n)
		}
		if err := c.Set(strings.ToLower(strings.TrimSpace(k)), v, baseDir); err != nil {
			return c, fmt.Errorf("%s line %d: %v", path, n, err)
		}
	}
	return c, sc.Err()
}
```

- [ ] **Step 5: Run tests**

Run: `bash tools/udpfsd-patch.sh test`
Expected: `ok github.com/pcm720/udpfsd/internal/config`, all other packages `ok`.

- [ ] **Step 6: Make `build-udpfsd.sh` test every package (except chd)**

In `tools/build-udpfsd.sh` replace the two lines

```
go vet ./internal/fs/ ./cmd/...
go test ./internal/fs/ ./internal/fs/compression/zso/ ./internal/fs/compression/cso/
```

with

```
P=$(go list ./... | grep -v /chd)
go vet $P
go test $P
```

- [ ] **Step 7: Save the patch and commit**

```bash
bash tools/udpfsd-patch.sh save 0002-game-prep.patch
git add tools/udpfsd-patch.sh tools/build-udpfsd.sh patches/udpfsd/0002-game-prep.patch
git commit -m "udpfsd: udpfsd.cfg parser (patch 0002, part 1)"
```

---

### Task 2: Read-only mount table

**Files (dev tree):**
- Delete: `internal/fs/installdir.go`, `internal/fs/installdir_test.go`
- Create: `internal/fs/mounts.go`, `internal/fs/mounts_test.go`
- Modify: `internal/fs/backend.go`, `internal/fs/utils.go`, `internal/fs/fileops.go`

**Interfaces:**
- Consumes: nothing new.
- Produces: `fs.WithMount(name, hostPath string) BackendOptFunc`; `fs.WithInstallDir(p)` kept (= `WithMount("INSTALL", p)`); `fs.InstallDirName = "INSTALL"`; `fs.PrepMountName = ".udpfsd"`. Mounts are read-only; a real `fsroot/<name>` is hidden.

- [ ] **Step 1: Write the failing test** — `internal/fs/mounts_test.go` (replaces `installdir_test.go`; keep its helpers `mkfile`, `listDir`, `readAll`, `eq` by moving them here unchanged)

```go
package fs

import (
	"os"
	"path/filepath"
	"sort"
	"testing"
)

func mkfile(t *testing.T, path string, data string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(data), 0644); err != nil {
		t.Fatal(err)
	}
}

func listDir(t *testing.T, b *Backend, path string) []string {
	t.Helper()
	h, _, err := b.Open(path, os.O_RDONLY, true)
	if err != nil || h < 0 {
		t.Fatalf("open dir %q: handle %d err %v", path, h, err)
	}
	defer b.Close(h)
	var names []string
	for {
		ok, name, _, err := b.Dread(h)
		if err != nil {
			t.Fatalf("dread %q: %v", path, err)
		}
		if !ok {
			break
		}
		names = append(names, name)
	}
	sort.Strings(names)
	return names
}

func readAll(t *testing.T, b *Backend, path string) (string, error) {
	t.Helper()
	h, _, err := b.Open(path, os.O_RDONLY, false)
	if err != nil {
		return "", err
	}
	defer b.Close(h)
	buf := make([]byte, 4096)
	n, data, err := b.Read(h, uint32(len(buf)), buf)
	if err != nil {
		return "", err
	}
	return string(data[:n]), nil
}

func eq(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

type fixture struct {
	base, root, dvd, cd, inst, prep string
}

func newFixture(t *testing.T) fixture {
	t.Helper()
	b := t.TempDir()
	f := fixture{base: b, root: filepath.Join(b, "root"), dvd: filepath.Join(b, "dvd"),
		cd: filepath.Join(b, "cd"), inst: filepath.Join(b, "queue"), prep: filepath.Join(b, "prep")}
	mkfile(t, filepath.Join(f.root, "DVD", "shadowed.iso"), "shadowed")
	mkfile(t, filepath.Join(f.root, "other.txt"), "root-other")
	mkfile(t, filepath.Join(f.dvd, "A.iso"), "dvd-a")
	mkfile(t, filepath.Join(f.cd, "C.iso"), "cd-c")
	mkfile(t, filepath.Join(f.inst, "sub", "B.iso"), "queue-b")
	mkfile(t, filepath.Join(f.prep, "manifest.txt"), "udpfsd-manifest 1\n")
	mkfile(t, filepath.Join(b, "secret.txt"), "secret")
	return f
}

func (f fixture) backend(t *testing.T, withRoot, readOnly bool) *Backend {
	t.Helper()
	opts := []BackendOptFunc{WithMount("DVD", f.dvd), WithMount("CD", f.cd),
		WithInstallDir(f.inst), WithMount(PrepMountName, f.prep)}
	if withRoot {
		opts = append(opts, WithFSRoot(f.root))
	}
	if readOnly {
		opts = append(opts, WithReadOnly())
	}
	b, err := NewBackend(opts...)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestMountsListedInRoot(t *testing.T) {
	f := newFixture(t)
	b := f.backend(t, true, true)
	if got := listDir(t, b, "/"); !eq(got, []string{".udpfsd", "CD", "DVD", "INSTALL", "other.txt"}) {
		t.Fatalf("root %v", got)
	}
	if got := listDir(t, b, "/DVD"); !eq(got, []string{"A.iso"}) { // real root/DVD hidden
		t.Fatalf("/DVD %v", got)
	}
	if got := listDir(t, b, "/INSTALL/sub"); !eq(got, []string{"B.iso"}) {
		t.Fatalf("/INSTALL/sub %v", got)
	}
}

func TestMountFilesReadable(t *testing.T) {
	f := newFixture(t)
	b := f.backend(t, true, true)
	for path, want := range map[string]string{
		"/DVD/A.iso":              "dvd-a",
		"CD/C.iso":                "cd-c",
		"\\INSTALL\\sub\\B.iso":   "queue-b",
		"/.udpfsd/manifest.txt":   "udpfsd-manifest 1\n",
		"/other.txt":              "root-other",
		"/DVD/../other.txt":       "root-other",
	} {
		if got, err := readAll(t, b, path); err != nil || got != want {
			t.Fatalf("read %q = %q, %v; want %q", path, got, err, want)
		}
	}
}

func TestMountNoEscape(t *testing.T) {
	f := newFixture(t)
	b := f.backend(t, true, true)
	for _, p := range []string{"/DVD/../../secret.txt", "/../secret.txt", "/INSTALL/../../queue/sub/B.iso",
		"/.udpfsd/../../secret.txt", "/DVD/shadowed.iso"} {
		if _, err := readAll(t, b, p); err == nil {
			t.Fatalf("read %q succeeded", p)
		}
	}
}

func TestMountsReadOnlyEvenWithoutRO(t *testing.T) {
	f := newFixture(t)
	b := f.backend(t, true, false)
	for _, m := range []string{"/DVD", "/CD", "/INSTALL", "/.udpfsd"} {
		if _, _, err := b.Open(m+"/new.iso", os.O_RDWR|os.O_CREATE, false); err == nil {
			t.Fatalf("create in %s succeeded", m)
		}
		if err := b.Mkdir(m + "/x"); err == nil {
			t.Fatalf("mkdir in %s succeeded", m)
		}
	}
	if err := b.Remove("/DVD/A.iso"); err == nil {
		t.Fatal("remove in mount succeeded")
	}
	if err := b.Rmdir("/INSTALL/sub"); err == nil {
		t.Fatal("rmdir in mount succeeded")
	}
	if err := b.Mkdir("/newdir"); err != nil { // fsroot stays writable without -ro
		t.Fatalf("mkdir in fsroot: %v", err)
	}
}

func TestMountsOnlyMode(t *testing.T) {
	f := newFixture(t)
	b := f.backend(t, false, true)
	if got := listDir(t, b, "/"); !eq(got, []string{".udpfsd", "CD", "DVD", "INSTALL"}) {
		t.Fatalf("root %v", got)
	}
	if _, err := readAll(t, b, "/other.txt"); err == nil {
		t.Fatal("fsroot file served without fsroot")
	}
	if _, _, err := b.Open("/", os.O_RDONLY, false); err == nil {
		t.Fatal("opened virtual root as a file")
	}
}

func TestNoMountsUnchanged(t *testing.T) {
	base := t.TempDir()
	mkfile(t, filepath.Join(base, "INSTALL", "x.iso"), "x")
	b, err := NewBackend(WithFSRoot(base), WithReadOnly())
	if err != nil {
		t.Fatal(err)
	}
	if got := listDir(t, b, "/"); !eq(got, []string{"INSTALL"}) {
		t.Fatalf("root %v", got)
	}
	if got, err := readAll(t, b, "/INSTALL/x.iso"); err != nil || got != "x" {
		t.Fatalf("read = %q, %v", got, err)
	}
}

func TestMountMustExist(t *testing.T) {
	if _, err := NewBackend(WithMount("DVD", filepath.Join(t.TempDir(), "missing"))); err == nil {
		t.Fatal("missing mount dir accepted")
	}
}
```

- [ ] **Step 2: Run to verify it fails**

`git -C build/udpfsd-dev rm -q internal/fs/installdir_test.go`, then `bash tools/udpfsd-patch.sh test`
Expected: FAIL — `undefined: WithMount`, `undefined: PrepMountName`.

- [ ] **Step 3: Implement** — delete `internal/fs/installdir.go`; create `internal/fs/mounts.go`

```go
package fs

import (
	"fmt"
	"io/fs"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// InstallDirName is the batch-install mount (udpfsd.cfg "install").
const InstallDirName = "INSTALL"

// PrepMountName is where prepared data (manifest, jackets, launcher) is served.
const PrepMountName = ".udpfsd"

// virtualRoot stands for "/" when no fsroot is served.
const virtualRoot = "\x00udpfsd-virtual-root"

type mount struct {
	name string // client-visible top-level name
	dir  string // absolute host directory
}

// WithMount serves hostPath read-only as /<name>.
func WithMount(name, hostPath string) func(s *Backend) {
	return func(s *Backend) {
		if hostPath != "" {
			s.mounts = append(s.mounts, mount{name: name, dir: hostPath})
		}
	}
}

// WithInstallDir serves path read-only as /INSTALL.
func WithInstallDir(path string) func(s *Backend) { return WithMount(InstallDirName, path) }

func (s *Backend) initMounts() error {
	for i := range s.mounts {
		abs, err := filepath.Abs(s.mounts[i].dir)
		if err != nil {
			return err
		}
		if st, err := os.Stat(abs); err != nil || !st.IsDir() {
			return fmt.Errorf("mount /%s: %s is not a directory", s.mounts[i].name, s.mounts[i].dir)
		}
		s.mounts[i].dir = abs
		if s.fsRoot != "" {
			if st, err := os.Stat(filepath.Join(s.fsRoot, s.mounts[i].name)); err == nil && st.IsDir() {
				log.Printf("fs: warning: %s/%s is hidden by a mount", s.fsRoot, s.mounts[i].name)
			}
		}
	}
	return nil
}

// resolveMount maps a cleaned client path to a mount, if it addresses one.
func (s *Backend) resolveMount(clientPath string) (string, bool, bool) {
	for _, m := range s.mounts {
		if clientPath == m.name {
			return m.dir, true, true
		}
		if rest, ok := strings.CutPrefix(clientPath, m.name+string(os.PathSeparator)); ok {
			p, ok := underRoot(m.dir, rest)
			return p, ok, true
		}
	}
	return "", false, false
}

// underRoot joins rel to root and checks the result stays inside root.
func underRoot(root, rel string) (string, bool) {
	resolved, err := filepath.Abs(filepath.Join(root, rel))
	if err != nil {
		return "", false
	}
	cleanRoot := filepath.Clean(root)
	comparableRoot := strings.TrimSuffix(cleanRoot, string(os.PathSeparator))
	if !strings.HasPrefix(resolved, comparableRoot+string(os.PathSeparator)) && resolved != cleanRoot {
		return "", false
	}
	return resolved, true
}

// inMount reports whether a resolved host path lies in any mount.
func (s *Backend) inMount(resolved string) bool {
	if resolved == "" || resolved == virtualRoot {
		return false
	}
	for _, m := range s.mounts {
		if resolved == m.dir || strings.HasPrefix(resolved, m.dir+string(os.PathSeparator)) {
			return true
		}
	}
	return false
}

// isRootDir reports whether a resolved host path is the client's "/".
func (s *Backend) isRootDir(resolved string) bool {
	if resolved == virtualRoot {
		return true
	}
	return s.fsRoot != "" && resolved == filepath.Clean(s.fsRoot)
}

type renamedEntry struct {
	fs.DirEntry
	name string
}

func (e renamedEntry) Name() string { return e.name }

// rootEntries lists "/" with every mount added; real fsroot folders of
// the same names are hidden.
func (s *Backend) rootEntries(resolved string) ([]os.DirEntry, error) {
	hidden := map[string]bool{}
	for _, m := range s.mounts {
		hidden[m.name] = true
	}
	var entries []os.DirEntry
	if resolved != virtualRoot {
		real, err := os.ReadDir(resolved)
		if err != nil {
			return nil, err
		}
		for _, e := range real {
			if !hidden[e.Name()] {
				entries = append(entries, e)
			}
		}
	}
	for _, m := range s.mounts {
		info, err := os.Stat(m.dir)
		if err != nil {
			return nil, err
		}
		entries = append(entries, renamedEntry{fs.FileInfoToDirEntry(info), m.name})
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].Name() < entries[j].Name() })
	return entries, nil
}
```

`internal/fs/backend.go`: replace the field `installDir string // served read-only as /INSTALL` with `mounts []mount // read-only top-level mounts`, and replace `if err := s.initInstallDir(); err != nil {` with `if err := s.initMounts(); err != nil {`.

`internal/fs/utils.go`, in `PrintFSInfo` replace the `installDir` block with:

```go
	for _, m := range s.mounts {
		log.Printf("fs: mounted %s as /%s (read-only)", m.dir, m.name)
	}
```

and in `resolvePath` replace the block starting `if s.fsRoot == "" && s.installDir == "" {` … through the `if s.installDir != "" { … }` block with:

```go
	if s.fsRoot == "" && len(s.mounts) == 0 {
		return "", false
	}

	// Normalize client path and join with fs root
	clientPath = strings.TrimLeft(clientPath, "/\\")
	// Convert backslashes to forward slashes before FromSlash
	clientPath = filepath.Clean(filepath.FromSlash(strings.ReplaceAll(clientPath, "\\", "/")))

	// Mounts first (after Clean, so "DVD/../x" is an fsroot path and
	// "DVD/../../x" is rejected).
	if p, ok, isMount := s.resolveMount(clientPath); isMount {
		return p, ok
	}
	if s.fsRoot == "" {
		if clientPath == "." {
			return virtualRoot, true
		}
		return "", false
	}
```

`internal/fs/fileops.go`: replace every `s.inInstallDir(` with `s.inMount(`; replace `if s.installDir != "" && s.isRootDir(fullPath) {` with `if len(s.mounts) > 0 && s.isRootDir(fullPath) {`; in the directory branch replace `statPath = s.installDir` with `statPath = s.mounts[0].dir`.

- [ ] **Step 4: Run tests**

Run: `bash tools/udpfsd-patch.sh test`
Expected: all packages `ok` (mount tests pass; no references to `installDir` remain — `grep -rn installDir build/udpfsd-dev/internal` prints nothing).

- [ ] **Step 5: Save and commit**

```bash
bash tools/udpfsd-patch.sh save 0002-game-prep.patch
git add patches/udpfsd/0002-game-prep.patch
git commit -m "udpfsd: generalize -install-dir into a read-only mount table (patch 0002)"
```

---

### Task 3: ISO probe and logical reader (`internal/prep/iso.go`)

**Files (dev tree):** Create `internal/prep/iso.go`, `internal/prep/iso_test.go`, `internal/prep/testiso_test.go`

**Interfaces:**
- Consumes: `compression.Open(path, cacheSize) interfaces.FileObject` (udpfsd).
- Produces: `prep.DiscInfo{ID, VolumeID string; Bytes int64; PVDBlocks, Sectors, Layer1 uint32; DVD bool}`; `prep.OpenLogical(path string) (r io.ReaderAt, size int64, closer io.Closer, err error)`; `prep.Probe(r io.ReaderAt, size int64, hint string) (DiscInfo, error)` with `hint` `"CD"`, `"DVD"` or `""`; `prep.HintFromPath(clientPath string) string`; `prep.ValidID(id string) bool`.

- [ ] **Step 1: Test helpers** — `internal/prep/testiso_test.go` (synthetic PS2 ISO + minimal ZSO writer; mirrors `test/host/isobuild.h`)

```go
package prep

import (
	"encoding/binary"
	"os"
	"testing"
)

const sec = 2048

func put32both(b []byte, v uint32) {
	binary.LittleEndian.PutUint32(b, v)
	binary.BigEndian.PutUint32(b[4:], v)
}

func dirrec(b []byte, lba, size uint32, name string, dir bool) int {
	n := 33 + len(name)
	if n%2 == 1 {
		n++
	}
	b[0] = byte(n)
	put32both(b[2:], lba)
	put32both(b[10:], size)
	if dir {
		b[25] = 2
	}
	b[32] = byte(len(name))
	copy(b[33:], name)
	return n
}

// buildISO returns a PS2 disc image of totalSectors sectors whose PVD
// says pvdBlocks. cnf == "" means no SYSTEM.CNF; udf adds BEA01/NSR02.
func buildISO(volid string, pvdBlocks, totalSectors uint32, cnf string, udf bool) []byte {
	img := make([]byte, int(totalSectors)*sec)
	pvd := img[16*sec:]
	pvd[0] = 1
	copy(pvd[1:], "CD001")
	pvd[6] = 1
	for i := 0; i < 32; i++ {
		pvd[40+i] = ' '
	}
	copy(pvd[40:], volid)
	put32both(pvd[80:], pvdBlocks)
	binary.LittleEndian.PutUint16(pvd[128:], sec)
	dirrec(pvd[156:], 24, sec, "\x00", true)
	term := img[17*sec:]
	term[0] = 255
	copy(term[1:], "CD001")
	if udf {
		copy(img[18*sec+1:], "BEA01")
		copy(img[19*sec+1:], "NSR02")
	}
	root := img[24*sec:]
	off := dirrec(root, 24, sec, "\x00", true)
	off += dirrec(root[off:], 24, sec, "\x01", true)
	if cnf != "" {
		dirrec(root[off:], 26, uint32(len(cnf)), "SYSTEM.CNF;1", false)
		copy(img[26*sec:], cnf)
	}
	return img
}

// writeZSO stores data as a ZSO with every block flagged uncompressed.
func writeZSO(t *testing.T, path string, data []byte) {
	t.Helper()
	const bs = 2048
	n := (len(data) + bs - 1) / bs
	hdr := make([]byte, 24)
	copy(hdr, "ZSO\x00")
	binary.LittleEndian.PutUint32(hdr[4:], 24)
	binary.LittleEndian.PutUint64(hdr[8:], uint64(len(data)))
	binary.LittleEndian.PutUint32(hdr[16:], bs)
	offs := make([]byte, 4*n)
	pos := uint32(24 + 4*n)
	for i := 0; i < n; i++ {
		binary.LittleEndian.PutUint32(offs[4*i:], pos|0x80000000)
		pos += bs
	}
	out := append(hdr, offs...)
	pad := make([]byte, n*bs-len(data))
	out = append(append(out, data...), pad...)
	if err := os.WriteFile(path, out, 0644); err != nil {
		t.Fatal(err)
	}
}

const cnfOK = "BOOT2 = cdrom0:\\SLUS_203.12;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"
```

- [ ] **Step 2: Write the failing test** — `internal/prep/iso_test.go`

```go
package prep

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

func probeBytes(t *testing.T, img []byte, hint string) (DiscInfo, error) {
	t.Helper()
	return Probe(bytes.NewReader(img), int64(len(img)), hint)
}

func TestProbeCD(t *testing.T) {
	d, err := probeBytes(t, buildISO("GT4", 1000, 1000, cnfOK, false), "")
	if err != nil {
		t.Fatal(err)
	}
	if d.ID != "SLUS_203.12" || d.VolumeID != "GT4" || d.Sectors != 1000 || d.DVD || d.Layer1 != 0 {
		t.Fatalf("%+v", d)
	}
}

func TestProbeHintsAndUDF(t *testing.T) {
	d, _ := probeBytes(t, buildISO("X", 1000, 1000, cnfOK, true), "")
	if !d.DVD {
		t.Fatal("UDF not DVD")
	}
	d, _ = probeBytes(t, buildISO("X", 1000, 1000, cnfOK, true), "CD")
	if d.DVD {
		t.Fatal("CD hint ignored")
	}
	d, _ = probeBytes(t, buildISO("X", 1000, 1000, cnfOK, false), "DVD")
	if !d.DVD {
		t.Fatal("DVD hint ignored")
	}
}

func TestProbeRejects(t *testing.T) {
	if _, err := probeBytes(t, make([]byte, 64*sec), ""); err == nil {
		t.Fatal("blank accepted")
	}
	if _, err := probeBytes(t, buildISO("X", 1000, 1000, "", false), ""); err == nil {
		t.Fatal("no SYSTEM.CNF accepted")
	}
	if _, err := probeBytes(t, buildISO("X", 1000, 1000, "BOOT2 = cdrom0:\\MAIN.ELF;1\n", false), ""); err == nil {
		t.Fatal("non-ID BOOT2 accepted")
	}
	img := buildISO("X", 1000, 1000, cnfOK, false)
	if _, err := probeBytes(t, img[:999*sec], ""); err == nil {
		t.Fatal("truncated accepted")
	}
}

func TestProbeDVD9LayerBreak(t *testing.T) {
	img := buildISO("DL", 1000, 1500, cnfOK, true)
	copy(img[1000*sec:], img[16*sec:17*sec]) // layer-1 PVD at maxLBA
	d, err := probeBytes(t, img, "")
	if err != nil || d.Layer1 != 1000-16 || d.Sectors != 1500 {
		t.Fatalf("%+v %v", d, err)
	}
}

func TestHintFromPath(t *testing.T) {
	cases := map[string]string{"/DVD/x.iso": "DVD", "/CD/a/x.iso": "CD", "/GAMES/x.iso": "",
		"/GAMES/cd/x.iso": "CD", "/DVD/CD/x.iso": "CD", "/DVDs/x.iso": ""}
	for p, want := range cases {
		if got := HintFromPath(p); got != want {
			t.Fatalf("%s: %q want %q", p, got, want)
		}
	}
}

func TestOpenLogicalISOAndZSO(t *testing.T) {
	dir := t.TempDir()
	img := buildISO("Z", 1000, 1000, cnfOK, false)
	iso := filepath.Join(dir, "g.iso")
	os.WriteFile(iso, img, 0644)
	zso := filepath.Join(dir, "g.zso")
	writeZSO(t, zso, img)
	for _, p := range []string{iso, zso} {
		r, size, c, err := OpenLogical(p)
		if err != nil {
			t.Fatalf("%s: %v", p, err)
		}
		d, err := Probe(r, size, "")
		c.Close()
		if err != nil || d.ID != "SLUS_203.12" || size != int64(len(img)) {
			t.Fatalf("%s: %+v %v size %d", p, d, err, size)
		}
	}
}

func TestValidID(t *testing.T) {
	if !ValidID("SCES_503.62") || ValidID("SCES-50362") || ValidID("sces_503.62") || ValidID("SCES_503.6") {
		t.Fatal("ValidID")
	}
}
```

- [ ] **Step 3: Run to verify it fails**

Run: `bash tools/udpfsd-patch.sh test` — Expected: FAIL, `undefined: Probe`.

- [ ] **Step 4: Implement** — `internal/prep/iso.go`

```go
// Package prep prepares game metadata, jackets and the manifest that the
// PSX DESR installer reads from udpfs:/.udpfsd.
package prep

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"

	"github.com/pcm720/udpfsd/internal/fs/compression"
)

const isoSector = 2048
const cdMaxSectors = 445500 // 99-minute CD; larger without a hint = DVD

// DiscInfo is what the installer needs to know about one disc image.
type DiscInfo struct {
	ID        string // "SLUS_203.12"
	VolumeID  string
	Bytes     int64
	PVDBlocks uint32
	Sectors   uint32
	Layer1    uint32
	DVD       bool
}

var idRe = regexp.MustCompile(`^[A-Z]{4}_[0-9]{3}\.[0-9]{2}$`)

// ValidID reports whether id has the BOOT form XXXX_NNN.NN.
func ValidID(id string) bool { return idRe.MatchString(id) }

type readerAtCloser struct {
	mu sync.Mutex
	f  interface {
		io.ReadSeeker
		io.Closer
	}
}

func (r *readerAtCloser) ReadAt(p []byte, off int64) (int, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, err := r.f.Seek(off, io.SeekStart); err != nil {
		return 0, err
	}
	return io.ReadFull(r.f, p)
}

func (r *readerAtCloser) Close() error { return r.f.Close() }

// OpenLogical opens an .iso as-is or a .zso through udpfsd's decompressor,
// returning the logical ISO stream.
func OpenLogical(path string) (io.ReaderAt, int64, io.Closer, error) {
	if strings.EqualFold(filepath.Ext(path), ".zso") {
		fo := compression.Open(path, 8)
		if fo == nil {
			return nil, 0, nil, fmt.Errorf("%s: not a valid ZSO", path)
		}
		st, err := fo.Stat()
		if err != nil {
			fo.Close()
			return nil, 0, nil, err
		}
		r := &readerAtCloser{f: fo}
		return r, st.Size(), r, nil
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, 0, nil, err
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, 0, nil, err
	}
	return f, st.Size(), f, nil
}

// HintFromPath returns "CD"/"DVD" from the nearest CD/DVD directory
// component of a client path, else "".
func HintFromPath(p string) string {
	hint := ""
	parts := strings.Split(strings.ReplaceAll(p, "\\", "/"), "/")
	for _, c := range parts[:len(parts)-1] {
		switch strings.ToUpper(c) {
		case "CD":
			hint = "CD"
		case "DVD":
			hint = "DVD"
		}
	}
	return hint
}

func readSector(r io.ReaderAt, lba uint32, buf []byte) error {
	_, err := r.ReadAt(buf[:isoSector], int64(lba)*isoSector)
	return err
}

func findInDir(r io.ReaderAt, lba, size uint32, target string) (uint32, uint32, error) {
	buf := make([]byte, isoSector)
	if size > 64*isoSector {
		size = 64 * isoSector
	}
	for left := size; left > 0; lba++ {
		if err := readSector(r, lba, buf); err != nil {
			return 0, 0, err
		}
		chunk := left
		if chunk > isoSector {
			chunk = isoSector
		}
		for off := uint32(0); off+33 < chunk; {
			rl := uint32(buf[off])
			if rl == 0 || rl < 34 || off+rl > isoSector {
				break
			}
			nl := uint32(buf[off+32])
			name := string(buf[off+33 : off+33+min(nl, rl-33)])
			if name == target || strings.HasPrefix(name, target+";") {
				return binary.LittleEndian.Uint32(buf[off+2:]), binary.LittleEndian.Uint32(buf[off+10:]), nil
			}
			off += rl
		}
		if left <= isoSector {
			break
		}
		left -= isoSector
	}
	return 0, 0, errors.New("SYSTEM.CNF not found")
}

func bootID(cnf string) (string, error) {
	for _, line := range strings.Split(strings.ReplaceAll(cnf, "\r", ""), "\n") {
		k, v, ok := strings.Cut(line, "=")
		if !ok || strings.TrimSpace(k) != "BOOT2" {
			continue
		}
		v = strings.TrimSpace(v)
		if i := strings.LastIndex(v, ":"); i >= 0 {
			v = v[i+1:]
		}
		if i := strings.LastIndexAny(v, "\\/"); i >= 0 {
			v = v[i+1:]
		}
		if i := strings.IndexAny(v, "; \t"); i >= 0 {
			v = v[:i]
		}
		v = strings.ToUpper(v)
		if !ValidID(v) {
			return "", fmt.Errorf("BOOT2 %q is not a game ID", v)
		}
		return v, nil
	}
	return "", errors.New("SYSTEM.CNF has no BOOT2")
}

// Probe validates a PS2 disc image and extracts DiscInfo (same rules as
// the installer's src/iso9660.c).
func Probe(r io.ReaderAt, size int64, hint string) (DiscInfo, error) {
	var d DiscInfo
	d.Bytes = size
	pvd := make([]byte, isoSector)
	if err := readSector(r, 16, pvd); err != nil {
		return d, fmt.Errorf("read PVD: %w", err)
	}
	if pvd[0] != 1 || !bytes.Equal(pvd[1:6], []byte("CD001")) {
		return d, errors.New("not an ISO9660 image")
	}
	d.VolumeID = strings.TrimRight(string(pvd[40:72]), " \x00")
	d.PVDBlocks = binary.LittleEndian.Uint32(pvd[80:])
	if binary.LittleEndian.Uint16(pvd[128:]) != isoSector || d.PVDBlocks <= 16 {
		return d, errors.New("bad PVD block size/count")
	}
	if size%isoSector != 0 || size < int64(d.PVDBlocks)*isoSector || size/isoSector > 0xFFFFFFFF {
		return d, errors.New("image size does not match PVD")
	}
	d.Sectors = uint32(size / isoSector)
	lba, sz, err := findInDir(r, binary.LittleEndian.Uint32(pvd[158:]), binary.LittleEndian.Uint32(pvd[166:]), "SYSTEM.CNF")
	if err != nil {
		return d, err
	}
	if sz > isoSector {
		sz = isoSector
	}
	cnf := make([]byte, isoSector)
	if err := readSector(r, lba, cnf); err != nil {
		return d, err
	}
	if d.ID, err = bootID(string(cnf[:sz])); err != nil {
		return d, err
	}
	udf := false
	buf := make([]byte, isoSector)
	for l := uint32(17); l < 32; l++ {
		if readSector(r, l, buf) != nil {
			break
		}
		if s := string(buf[1:6]); s == "BEA01" || s == "NSR02" || s == "NSR03" {
			udf = true
			break
		}
	}
	switch hint {
	case "CD":
		d.DVD = false
	case "DVD":
		d.DVD = true
	default:
		d.DVD = udf || d.Sectors > cdMaxSectors
	}
	if d.DVD && d.Sectors > d.PVDBlocks {
		if readSector(r, d.PVDBlocks, buf) == nil && buf[0] == 1 && string(buf[1:6]) == "CD001" {
			d.Layer1 = d.PVDBlocks - 16
		}
	}
	return d, nil
}
```

- [ ] **Step 5: Run tests** — `bash tools/udpfsd-patch.sh test` — Expected: `ok .../internal/prep`.

- [ ] **Step 6: Save and commit**

```bash
bash tools/udpfsd-patch.sh save 0002-game-prep.patch
git add patches/udpfsd/0002-game-prep.patch
git commit -m "udpfsd: ISO/ZSO probe for game preparation (patch 0002)"
```

---

### Task 4: Titles (`internal/prep/titles.go`)

**Files (dev tree):** Create `internal/prep/titles.go`, `internal/prep/titles_test.go`

**Interfaces:**
- Produces: `prep.LoadGameList(path string) (map[string]string, error)`; `prep.CFGTitle(cfgDir, id string) (title string, ok bool)`; `prep.FileTitle(path string) string`; `prep.Title(id, cfgDir string, list map[string]string, path string) string`; `prep.Sanitize(s string, max int) string`.

- [ ] **Step 1: Write the failing test** — `internal/prep/titles_test.go`

```go
package prep

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"unicode/utf8"
)

func TestLoadGameListBothFormats(t *testing.T) {
	p := filepath.Join(t.TempDir(), "GameListPS2.txt")
	os.WriteFile(p, []byte("type     size flags           dma startup      name\n"+
		"DVD  666094KB               0 *u4 SLUS_210.90  Alien Homind\n"+
		"CD   525382KB               0 *u4 SLUS_201.47  Aliens vs Predator: Extinction\r\n"+
		"SLES_546.04 ^!Qu? pasa Neng^! El videojuego\n"+
		"garbage line\n"), 0644)
	m, err := LoadGameList(p)
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]string{"SLUS_210.90": "Alien Homind",
		"SLUS_201.47": "Aliens vs Predator: Extinction", "SLES_546.04": "!Qu? pasa Neng! El videojuego"}
	if len(m) != len(want) {
		t.Fatalf("%v", m)
	}
	for k, v := range want {
		if m[k] != v {
			t.Fatalf("%s = %q", k, m[k])
		}
	}
}

func TestCFGTitle(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "SCUS_971.01.cfg"), []byte("Title=Twisted Metal: Black\r\nSerial=SCUS-97101\r\n"), 0644)
	if got, ok := CFGTitle(dir, "SCUS_971.01"); !ok || got != "Twisted Metal: Black" {
		t.Fatalf("%q %v", got, ok)
	}
	if _, ok := CFGTitle(dir, "SLUS_203.12"); ok {
		t.Fatal("missing cfg found")
	}
	if _, ok := CFGTitle("", "SCUS_971.01"); ok {
		t.Fatal("no cfg dir")
	}
}

func TestFileTitle(t *testing.T) {
	cases := map[string]string{
		"/x/Grand Theft Auto - San Andreas (USA) (v3.00).zso": "Grand Theft Auto - San Andreas",
		"HALF LIFE.zso":                    "HALF LIFE",
		"Game [!] (En,Fr).iso":             "Game",
		"(Only Parens).iso":                "(Only Parens)",
	}
	for in, want := range cases {
		if got := FileTitle(in); got != want {
			t.Fatalf("%q -> %q want %q", in, got, want)
		}
	}
}

func TestTitlePrecedence(t *testing.T) {
	dir := t.TempDir()
	os.WriteFile(filepath.Join(dir, "SLUS_203.12.cfg"), []byte("Title=From Cfg\n"), 0644)
	list := map[string]string{"SLUS_203.12": "From List", "SLUS_203.13": "Listed"}
	if got := Title("SLUS_203.12", dir, list, "f.iso"); got != "From Cfg" {
		t.Fatal(got)
	}
	if got := Title("SLUS_203.13", dir, list, "f.iso"); got != "Listed" {
		t.Fatal(got)
	}
	if got := Title("SLUS_203.14", dir, list, "/a/Name (USA).iso"); got != "Name" {
		t.Fatal(got)
	}
}

func TestSanitize(t *testing.T) {
	if got := Sanitize("a\tb\r\nc\x01d  ", 63); got != "a b c d" {
		t.Fatalf("%q", got)
	}
	long := strings.Repeat("é", 40) // 80 bytes
	got := Sanitize(long, 63)
	if len(got) > 63 || !utf8.ValidString(got) {
		t.Fatalf("len %d valid %v", len(got), utf8.ValidString(got))
	}
}
```

- [ ] **Step 2: Run to verify it fails** — `bash tools/udpfsd-patch.sh test` — Expected: FAIL, `undefined: LoadGameList`.

- [ ] **Step 3: Implement** — `internal/prep/titles.go`

```go
package prep

import (
	"bufio"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"unicode/utf8"
)

// LoadGameList reads ID -> name from GameListPS2.txt-style files: any line
// containing a BOOT-form ID token; the name is the text after that token.
func LoadGameList(path string) (map[string]string, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	m := map[string]string{}
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := strings.TrimRight(sc.Text(), "\r")
		fields := strings.Fields(line)
		for _, tok := range fields {
			if !ValidID(tok) {
				continue
			}
			i := strings.Index(line, tok) + len(tok)
			name := strings.ReplaceAll(strings.TrimSpace(line[i:]), "^!", "!")
			if name != "" {
				if _, dup := m[tok]; !dup {
					m[tok] = name
				}
			}
			break
		}
	}
	return m, sc.Err()
}

// CFGTitle returns Title= from <cfgDir>/<id>.cfg (OPL per-game config).
func CFGTitle(cfgDir, id string) (string, bool) {
	if cfgDir == "" {
		return "", false
	}
	data, err := os.ReadFile(filepath.Join(cfgDir, id+".cfg"))
	if err != nil {
		return "", false
	}
	for _, line := range strings.Split(string(data), "\n") {
		if v, ok := strings.CutPrefix(strings.TrimRight(line, "\r"), "Title="); ok && strings.TrimSpace(v) != "" {
			return strings.TrimSpace(v), true
		}
	}
	return "", false
}

var trailingGroup = regexp.MustCompile(`\s*[\(\[][^\)\]]*[\)\]]\s*$`)

// FileTitle derives a title from an image file name.
func FileTitle(path string) string {
	name := filepath.Base(strings.ReplaceAll(path, "\\", "/"))
	for _, ext := range []string{".iso", ".zso"} {
		if strings.HasSuffix(strings.ToLower(name), ext) {
			name = name[:len(name)-len(ext)]
		}
	}
	for {
		stripped := trailingGroup.ReplaceAllString(name, "")
		if stripped == name || strings.TrimSpace(stripped) == "" {
			break
		}
		name = stripped
	}
	return strings.TrimSpace(name)
}

// Title picks cfg Title= > game list > file name.
func Title(id, cfgDir string, list map[string]string, path string) string {
	if t, ok := CFGTitle(cfgDir, id); ok {
		return t
	}
	if t := list[id]; t != "" {
		return t
	}
	return FileTitle(path)
}

// Sanitize removes control characters (tabs and line breaks become one
// space), collapses spaces and truncates to max bytes at a rune boundary.
func Sanitize(s string, max int) string {
	var b strings.Builder
	space := false
	for _, r := range s {
		if r < 0x20 || r == 0x7f || r == ' ' {
			space = true
			continue
		}
		if space && b.Len() > 0 {
			b.WriteByte(' ')
		}
		space = false
		b.WriteRune(r)
	}
	out := b.String()
	for len(out) > max {
		_, size := utf8.DecodeLastRuneInString(out)
		out = out[:len(out)-size]
	}
	return strings.TrimSpace(out)
}
```

- [ ] **Step 4: Run tests** — Expected: `ok .../internal/prep`.

- [ ] **Step 5: Save and commit** — `bash tools/udpfsd-patch.sh save 0002-game-prep.patch && git add patches/udpfsd/0002-game-prep.patch && git commit -m "udpfsd: game titles from CFG, game list, file name (patch 0002)"`

---

### Task 5: Art lookup, jacket conversion, cover download

**Files (dev tree):** Create `internal/prep/art.go`, `internal/prep/art_test.go`, `internal/prep/download.go`, `internal/prep/download_test.go`

**Interfaces:**
- Produces: `prep.FindArt(artDir, id, imagePath string) string`; `prep.MakeJacket(src, dst string) error` (74x108 opaque PNG); `prep.JacketW = 74`, `prep.JacketH = 108`; `prep.Downloader{BaseURL string; Client *http.Client; CacheDir string}`; `(*Downloader).Fetch(id string) (path string, err error)`; `prep.ErrNotFound`; `prep.CoverURL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default"`.

- [ ] **Step 1: Write the failing tests** — `internal/prep/art_test.go`

```go
package prep

import (
	"image"
	"image/color"
	"image/jpeg"
	"image/png"
	"os"
	"path/filepath"
	"testing"
)

func writeImg(t *testing.T, path string, w, h int, jpg bool) {
	t.Helper()
	img := image.NewRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.Set(x, y, color.RGBA{uint8(x), uint8(y), 200, 255})
		}
	}
	f, _ := os.Create(path)
	defer f.Close()
	if jpg {
		jpeg.Encode(f, img, nil)
	} else {
		png.Encode(f, img)
	}
}

func TestFindArtOrder(t *testing.T) {
	art := t.TempDir()
	games := t.TempDir()
	game := filepath.Join(games, "Game (USA).zso")
	if FindArt(art, "SLUS_203.12", game) != "" {
		t.Fatal("found art in empty dirs")
	}
	writeImg(t, filepath.Join(games, "Game (USA).jpg"), 8, 8, true)
	if got := FindArt(art, "SLUS_203.12", game); filepath.Base(got) != "Game (USA).jpg" {
		t.Fatal(got)
	}
	writeImg(t, filepath.Join(art, "SLUS_203.12.png"), 8, 8, false)
	if got := FindArt(art, "SLUS_203.12", game); filepath.Base(got) != "SLUS_203.12.png" {
		t.Fatal(got)
	}
	writeImg(t, filepath.Join(art, "SLUS_203.12_COV2.jpg"), 8, 8, true)
	if got := FindArt(art, "SLUS_203.12", game); filepath.Base(got) != "SLUS_203.12_COV2.jpg" {
		t.Fatal(got)
	}
	writeImg(t, filepath.Join(art, "SLUS_203.12_COV.jpg"), 8, 8, true)
	if got := FindArt(art, "SLUS_203.12", game); filepath.Base(got) != "SLUS_203.12_COV.jpg" {
		t.Fatal(got)
	}
	writeImg(t, filepath.Join(art, "SLUS_203.12_ICO.png"), 8, 8, false) // never used
	if got := FindArt(art, "SLUS_203.12", game); filepath.Base(got) != "SLUS_203.12_COV.jpg" {
		t.Fatal(got)
	}
}

func TestMakeJacket(t *testing.T) {
	dir := t.TempDir()
	for _, jpg := range []bool{false, true} {
		src := filepath.Join(dir, "src")
		writeImg(t, src, 512, 736, jpg)
		dst := filepath.Join(dir, "jkt", "out.png")
		if err := MakeJacket(src, dst); err != nil {
			t.Fatal(err)
		}
		f, _ := os.Open(dst)
		cfg, format, err := image.DecodeConfig(f)
		f.Close()
		if err != nil || format != "png" || cfg.Width != JacketW || cfg.Height != JacketH {
			t.Fatalf("%v %s %+v", err, format, cfg)
		}
		if cfg.ColorModel != color.RGBAModel && cfg.ColorModel != color.NRGBAModel {
			t.Fatalf("model %v", cfg.ColorModel)
		}
	}
	os.WriteFile(filepath.Join(dir, "bad"), []byte("not an image"), 0644)
	if err := MakeJacket(filepath.Join(dir, "bad"), filepath.Join(dir, "x.png")); err == nil {
		t.Fatal("bad image accepted")
	}
}
```

`internal/prep/download_test.go`

```go
package prep

import (
	"errors"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"
	"time"
)

func TestDownloadFetch(t *testing.T) {
	var asked []string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		asked = append(asked, r.URL.Path)
		if r.URL.Path == "/SLUS-20312.jpg" {
			w.Write([]byte("jpegdata"))
			return
		}
		http.NotFound(w, r)
	}))
	defer srv.Close()
	d := &Downloader{BaseURL: srv.URL, Client: srv.Client(), CacheDir: t.TempDir()}
	p, err := d.Fetch("SLUS_203.12")
	if err != nil {
		t.Fatal(err)
	}
	if b, _ := os.ReadFile(p); string(b) != "jpegdata" {
		t.Fatalf("%q", b)
	}
	if _, err := d.Fetch("SLUS_999.99"); !errors.Is(err, ErrNotFound) {
		t.Fatalf("404: %v", err)
	}
	n := len(asked)
	if _, err := d.Fetch("SLUS_999.99"); !errors.Is(err, ErrNotFound) || len(asked) != n {
		t.Fatal("404 not remembered for the session")
	}
	if p2, _ := d.Fetch("SLUS_203.12"); p2 != p || len(asked) != n {
		t.Fatal("cached cover re-downloaded")
	}
}

func TestDownloadOfflineIsFastAndSticky(t *testing.T) {
	d := &Downloader{BaseURL: "http://127.0.0.1:1", CacheDir: t.TempDir(),
		Client: &http.Client{Timeout: 2 * time.Second}}
	if _, err := d.Fetch("SLUS_203.12"); err == nil {
		t.Fatal("offline fetch succeeded")
	}
	start := time.Now()
	if _, err := d.Fetch("SLUS_203.13"); err == nil || time.Since(start) > 100*time.Millisecond {
		t.Fatal("offline not remembered")
	}
}
```

- [ ] **Step 2: Run to verify they fail** — Expected: FAIL, `undefined: FindArt`, `undefined: Downloader`.

- [ ] **Step 3: Implement** — `internal/prep/art.go`

```go
package prep

import (
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	"image/png"
	"os"
	"path/filepath"
	"strings"
)

// JacketW/JacketH is the DESR XMB jacket size (as PSX-XMB-Manager writes).
const (
	JacketW = 74
	JacketH = 108
)

var artExts = []string{".png", ".jpg", ".jpeg", ".gif"}

func firstExisting(base string) string {
	for _, e := range artExts {
		for _, ext := range []string{e, strings.ToUpper(e)} {
			if st, err := os.Stat(base + ext); err == nil && !st.IsDir() {
				return base + ext
			}
		}
	}
	return ""
}

// FindArt returns the best local jacket source or "".
func FindArt(artDir, id, imagePath string) string {
	if artDir != "" {
		for _, n := range []string{id + "_COV", id + "_COV2", id} {
			if p := firstExisting(filepath.Join(artDir, n)); p != "" {
				return p
			}
		}
	}
	if imagePath != "" {
		base := imagePath[:len(imagePath)-len(filepath.Ext(imagePath))]
		if p := firstExisting(base); p != "" {
			return p
		}
	}
	return ""
}

// MakeJacket decodes src (PNG/JPEG/GIF), box-filters it to 74x108 and
// writes an opaque, non-interlaced PNG to dst (temp file + rename).
func MakeJacket(src, dst string) error {
	f, err := os.Open(src)
	if err != nil {
		return err
	}
	img, _, err := image.Decode(f)
	f.Close()
	if err != nil {
		return fmt.Errorf("%s: %w", src, err)
	}
	b := img.Bounds()
	if b.Dx() == 0 || b.Dy() == 0 {
		return fmt.Errorf("%s: empty image", src)
	}
	out := image.NewRGBA(image.Rect(0, 0, JacketW, JacketH))
	for y := 0; y < JacketH; y++ {
		y0 := b.Min.Y + y*b.Dy()/JacketH
		y1 := max(y0+1, b.Min.Y+(y+1)*b.Dy()/JacketH)
		for x := 0; x < JacketW; x++ {
			x0 := b.Min.X + x*b.Dx()/JacketW
			x1 := max(x0+1, b.Min.X+(x+1)*b.Dx()/JacketW)
			var r, g, bl, n uint64
			for sy := y0; sy < y1; sy++ {
				for sx := x0; sx < x1; sx++ {
					cr, cg, cb, _ := img.At(sx, sy).RGBA()
					r, g, bl, n = r+uint64(cr), g+uint64(cg), bl+uint64(cb), n+1
				}
			}
			i := out.PixOffset(x, y)
			out.Pix[i], out.Pix[i+1], out.Pix[i+2], out.Pix[i+3] =
				uint8(r/n>>8), uint8(g/n>>8), uint8(bl/n>>8), 255
		}
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	tmp := dst + ".tmp"
	w, err := os.Create(tmp)
	if err != nil {
		return err
	}
	if err := png.Encode(w, out); err != nil {
		w.Close()
		os.Remove(tmp)
		return err
	}
	if err := w.Close(); err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, dst)
}
```

`internal/prep/download.go`

```go
package prep

import (
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// CoverURL is the public PS2 cover repository (files named XXXX-NNNNN.jpg).
const CoverURL = "https://raw.githubusercontent.com/xlenore/ps2-covers/main/covers/default"

// ErrNotFound means the repository has no cover for the ID.
var ErrNotFound = errors.New("cover not found")

// Downloader fetches covers once per ID per session and caches them.
type Downloader struct {
	BaseURL  string
	Client   *http.Client
	CacheDir string

	mu      sync.Mutex
	missing map[string]bool
	offline bool
}

func serial(id string) string { // SLUS_203.12 -> SLUS-20312
	return strings.Replace(strings.Replace(id, "_", "-", 1), ".", "", 1)
}

// Fetch returns the cached cover path for id, downloading it if needed.
func (d *Downloader) Fetch(id string) (string, error) {
	dst := filepath.Join(d.CacheDir, "covers", id+".jpg")
	if st, err := os.Stat(dst); err == nil && st.Size() > 0 {
		return dst, nil
	}
	d.mu.Lock()
	if d.missing == nil {
		d.missing = map[string]bool{}
	}
	if d.offline {
		d.mu.Unlock()
		return "", errors.New("cover download disabled: offline")
	}
	if d.missing[id] {
		d.mu.Unlock()
		return "", ErrNotFound
	}
	d.mu.Unlock()

	client := d.Client
	if client == nil {
		client = &http.Client{Timeout: 10 * time.Second}
	}
	resp, err := client.Get(d.BaseURL + "/" + serial(id) + ".jpg")
	if err != nil {
		d.mu.Lock()
		d.offline = true // one transport error disables downloads this session
		d.mu.Unlock()
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotFound {
		d.mu.Lock()
		d.missing[id] = true
		d.mu.Unlock()
		return "", ErrNotFound
	}
	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("cover %s: HTTP %d", id, resp.StatusCode)
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return "", err
	}
	tmp := dst + ".tmp"
	f, err := os.Create(tmp)
	if err != nil {
		return "", err
	}
	_, err = io.Copy(f, io.LimitReader(resp.Body, 8<<20))
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp)
		return "", err
	}
	return dst, os.Rename(tmp, dst)
}
```

- [ ] **Step 4: Run tests** — Expected: `ok .../internal/prep`.

- [ ] **Step 5: Save and commit** — `bash tools/udpfsd-patch.sh save 0002-game-prep.patch && git add patches/udpfsd/0002-game-prep.patch && git commit -m "udpfsd: jacket art lookup, 74x108 conversion, cover download (patch 0002)"`

---

### Task 6: Scanner, cache, manifest, launcher copy

**Files (dev tree):** Create `internal/prep/scan.go`, `internal/prep/scan_test.go`

**Interfaces:**
- Consumes: `config.Config` (Task 1), `Probe/OpenLogical/HintFromPath` (Task 3), `Title/Sanitize/LoadGameList` (Task 4), `FindArt/MakeJacket/Downloader` (Task 5).
- Produces: `prep.Folder{Mount, Dir string}`; `prep.Options{Folders []Folder; CFGDir, ARTDir, Gamelist, CacheDir, Launcher string; Download bool; Auto bool; Downloader *Downloader}`; `prep.ServedDir(cacheDir string) string`; `prep.Run(o Options) (Result, error)`; `prep.Result{Games, Invalid int}`; manifest at `ServedDir/manifest.txt`, jackets at `ServedDir/jkt/<ID>.png`, launcher at `ServedDir/EXECUTE.KELF`.

Manifest rules (exact): header `udpfsd-manifest 1`, then ` launcher=<sha256 hex>:<bytes>` if a launcher was copied, then ` auto=1` or ` auto=0`. Entry columns joined with `\t`: client path (`/<Mount>/<rel with />`; `.zso` files get `.iso` appended), `ok` or `invalid:<reason sanitized, ≤40 bytes>`, ID or `-`, title (`Sanitize(…, 63)`) or `-`, bytes, `CD`/`DVD`, layer1, `jkt/<ID>.png` or `-`, `/CFG/<ID>.cfg` or `-`. Entries sorted by path. Written in two phases: after probing (local art only), then again after downloads.

- [ ] **Step 1: Write the failing test** — `internal/prep/scan_test.go`

```go
package prep

import (
	"crypto/sha256"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func readManifest(t *testing.T, cache string) (string, []string) {
	t.Helper()
	b, err := os.ReadFile(filepath.Join(ServedDir(cache), "manifest.txt"))
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Split(strings.TrimRight(string(b), "\n"), "\n")
	return lines[0], lines[1:]
}

func TestRunFullScan(t *testing.T) {
	base := t.TempDir()
	dvd := filepath.Join(base, "DVD")
	cfg := filepath.Join(base, "CFG")
	art := filepath.Join(base, "ART")
	for _, d := range []string{dvd, cfg, art} {
		os.MkdirAll(d, 0755)
	}
	img := buildISO("GT4", 1000, 1000, cnfOK, true)
	os.WriteFile(filepath.Join(dvd, "Gran Turismo 4 (USA).iso"), img, 0644)
	writeZSO(t, filepath.Join(dvd, "Gran Turismo 4 (USA).zso"), img) // same game twice
	other := buildISO("X", 1000, 1000, strings.Replace(cnfOK, "SLUS_203.12", "SLUS_210.90", 1), false)
	os.WriteFile(filepath.Join(dvd, "Alien\tHominid.iso"), other, 0644)
	os.WriteFile(filepath.Join(dvd, "broken.iso"), make([]byte, 40*2048), 0644)
	os.WriteFile(filepath.Join(dvd, "readme.txt"), []byte("x"), 0644)
	os.WriteFile(filepath.Join(cfg, "SLUS_203.12.cfg"), []byte("Title=Gran Turismo 4\n$Compatibility=3\n"), 0644)
	writeImg(t, filepath.Join(art, "SLUS_203.12_COV.jpg"), 300, 400, true)
	gl := filepath.Join(base, "GameListPS2.txt")
	os.WriteFile(gl, []byte("DVD 1KB 0 *u4 SLUS_210.90  Alien Hominid\n"), 0644)
	launcher := filepath.Join(base, "opl-launcher-EXECUTE.KELF")
	kelf := []byte(strings.Repeat("K", 4096))
	os.WriteFile(launcher, kelf, 0644)

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/SLUS-21090.jpg" {
			f, _ := os.Open(filepath.Join(art, "SLUS_203.12_COV.jpg"))
			defer f.Close()
			b, _ := os.ReadFile(f.Name())
			w.Write(b)
			return
		}
		http.NotFound(w, r)
	}))
	defer srv.Close()
	cache := filepath.Join(base, "cache")
	o := Options{Folders: []Folder{{Mount: "DVD", Dir: dvd}}, CFGDir: cfg, ARTDir: art, Gamelist: gl,
		CacheDir: cache, Launcher: launcher, Download: true, Auto: true,
		Downloader: &Downloader{BaseURL: srv.URL, Client: srv.Client(), CacheDir: cache}}
	res, err := Run(o)
	if err != nil {
		t.Fatal(err)
	}
	if res.Games != 3 || res.Invalid != 1 {
		t.Fatalf("%+v", res)
	}
	head, lines := readManifest(t, cache)
	sum := sha256.Sum256(kelf)
	if head != fmt.Sprintf("udpfsd-manifest 1 launcher=%x:%d auto=1", sum, len(kelf)) {
		t.Fatalf("header %q", head)
	}
	want := []string{
		"/DVD/Alien Hominid.iso\tok\tSLUS_210.90\tAlien Hominid\t2048000\tCD\t0\tjkt/SLUS_210.90.png\t-",
		"/DVD/Gran Turismo 4 (USA).iso\tok\tSLUS_203.12\tGran Turismo 4\t2048000\tDVD\t0\tjkt/SLUS_203.12.png\t/CFG/SLUS_203.12.cfg",
		"/DVD/Gran Turismo 4 (USA).zso.iso\tok\tSLUS_203.12\tGran Turismo 4\t2048000\tDVD\t0\tjkt/SLUS_203.12.png\t/CFG/SLUS_203.12.cfg",
	}
	// the tab in "Alien\tHominid.iso" is a real file name character: path keeps it,
	// so compare everything except that entry's path column.
	if len(lines) != 4 {
		t.Fatalf("%d lines: %q", len(lines), lines)
	}
	if !strings.HasPrefix(lines[0], "/DVD/Alien") || strings.Count(lines[0], "\t") < 8 && false {
		t.Fatal(lines[0])
	}
	for i, l := range lines[1:3] {
		if l != want[i+1] {
			t.Fatalf("line %d:\n%q\nwant\n%q", i+1, l, want[i+1])
		}
	}
	if !strings.HasPrefix(lines[3], "/DVD/broken.iso\tinvalid:") {
		t.Fatalf("invalid line %q", lines[3])
	}
	for _, id := range []string{"SLUS_203.12", "SLUS_210.90"} {
		if _, err := os.Stat(filepath.Join(ServedDir(cache), "jkt", id+".png")); err != nil {
			t.Fatalf("jacket %s: %v", id, err)
		}
	}
	if b, _ := os.ReadFile(filepath.Join(ServedDir(cache), "EXECUTE.KELF")); string(b) != string(kelf) {
		t.Fatal("launcher not copied")
	}
	if _, err := os.Stat(filepath.Join(cache, "scan.json")); err != nil {
		t.Fatal("no scan cache")
	}
}

func TestManifestLinesNeverSplit(t *testing.T) {
	base := t.TempDir()
	dir := filepath.Join(base, "G")
	os.MkdirAll(dir, 0755)
	img := buildISO("X", 1000, 1000, cnfOK, false)
	os.WriteFile(filepath.Join(dir, "a.iso"), img, 0644)
	cfg := filepath.Join(base, "CFG")
	os.MkdirAll(cfg, 0755)
	os.WriteFile(filepath.Join(cfg, "SLUS_203.12.cfg"), []byte("Title=Bad\tTitle "+strings.Repeat("x", 100)+"\n"), 0644)
	cache := filepath.Join(base, "c")
	if _, err := Run(Options{Folders: []Folder{{"GAMES", dir}}, CFGDir: cfg, CacheDir: cache}); err != nil {
		t.Fatal(err)
	}
	head, lines := readManifest(t, cache)
	if head != "udpfsd-manifest 1 auto=0" || len(lines) != 1 {
		t.Fatalf("%q %q", head, lines)
	}
	cols := strings.Split(lines[0], "\t")
	if len(cols) != 9 || len(cols[3]) > 63 || strings.ContainsAny(cols[3], "\r\n") {
		t.Fatalf("%q", cols)
	}
	if cols[7] != "-" { // no art, no downloads
		t.Fatalf("jacket %q", cols[7])
	}
}

func TestCacheSkipsUnchangedImages(t *testing.T) {
	base := t.TempDir()
	dir := filepath.Join(base, "G")
	os.MkdirAll(dir, 0755)
	p := filepath.Join(dir, "a.iso")
	os.WriteFile(p, buildISO("X", 1000, 1000, cnfOK, false), 0644)
	cache := filepath.Join(base, "c")
	o := Options{Folders: []Folder{{"GAMES", dir}}, CacheDir: cache}
	if _, err := Run(o); err != nil {
		t.Fatal(err)
	}
	probes := 0
	probeHook = func(string) { probes++ }
	defer func() { probeHook = nil }()
	if _, err := Run(o); err != nil || probes != 0 {
		t.Fatalf("unchanged image re-probed (%d) %v", probes, err)
	}
	os.WriteFile(p, buildISO("Y", 1000, 1001, cnfOK, false), 0644)
	if _, err := Run(o); err != nil || probes != 1 {
		t.Fatalf("changed image not re-probed (%d)", probes)
	}
}

func TestManifestWriteIsAtomic(t *testing.T) {
	cache := t.TempDir()
	served := ServedDir(cache)
	os.MkdirAll(served, 0755)
	if err := writeAtomic(filepath.Join(served, "manifest.txt"), []byte("udpfsd-manifest 1 auto=0\n")); err != nil {
		t.Fatal(err)
	}
	ents, _ := os.ReadDir(served)
	for _, e := range ents {
		if strings.HasSuffix(e.Name(), ".tmp") {
			t.Fatal("temp file left behind")
		}
	}
}
```

- [ ] **Step 2: Run to verify it fails** — Expected: FAIL, `undefined: Run`, `undefined: ServedDir`.

- [ ] **Step 3: Implement** — `internal/prep/scan.go`

```go
package prep

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
)

// Folder is a game folder served as /<Mount>.
type Folder struct{ Mount, Dir string }

// Options configures one preparation run.
type Options struct {
	Folders                                    []Folder
	CFGDir, ARTDir, Gamelist, CacheDir, Launcher string
	Download, Auto                              bool
	Downloader                                  *Downloader
}

// Result summarizes a run.
type Result struct{ Games, Invalid int }

type entry struct {
	Path    string // client path
	Host    string
	Size    int64
	ModTime int64
	Err     string
	Info    DiscInfo
	Title   string
	Jacket  bool
	CFG     bool
}

var probeHook func(path string) // test hook

// ServedDir is the folder served as /.udpfsd.
func ServedDir(cacheDir string) string { return filepath.Join(cacheDir, "served") }

func writeAtomic(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return err
	}
	tmp := path + ".tmp"
	if err := os.WriteFile(tmp, data, 0644); err != nil {
		os.Remove(tmp)
		return err
	}
	return os.Rename(tmp, path)
}

func clientPath(mount, rel string) string {
	p := "/" + mount + "/" + filepath.ToSlash(rel)
	if strings.EqualFold(filepath.Ext(p), ".zso") {
		p += ".iso" // udpfsd's virtual decompressed name
	}
	return p
}

func (o *Options) collect() []entry {
	var out []entry
	for _, f := range o.Folders {
		filepath.WalkDir(f.Dir, func(p string, d os.DirEntry, err error) error {
			if err != nil || d.IsDir() {
				return nil
			}
			ext := strings.ToLower(filepath.Ext(p))
			if ext != ".iso" && ext != ".zso" {
				return nil
			}
			st, err := d.Info()
			if err != nil {
				return nil
			}
			rel, _ := filepath.Rel(f.Dir, p)
			out = append(out, entry{Path: clientPath(f.Mount, rel), Host: p,
				Size: st.Size(), ModTime: st.ModTime().UnixNano()})
			return nil
		})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Path < out[j].Path })
	return out
}

func probe(e *entry) {
	if probeHook != nil {
		probeHook(e.Host)
	}
	r, size, c, err := OpenLogical(e.Host)
	if err != nil {
		e.Err = err.Error()
		return
	}
	defer c.Close()
	info, err := Probe(r, size, HintFromPath(e.Path))
	if err != nil {
		e.Err = err.Error()
		return
	}
	e.Info = info
}

func copyLauncher(src, served string) (string, error) {
	in, err := os.Open(src)
	if err != nil {
		return "", err
	}
	defer in.Close()
	h := sha256.New()
	tmp := filepath.Join(served, "EXECUTE.KELF.tmp")
	out, err := os.Create(tmp)
	if err != nil {
		return "", err
	}
	n, err := io.Copy(io.MultiWriter(out, h), in)
	if cerr := out.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		os.Remove(tmp)
		return "", err
	}
	if err := os.Rename(tmp, filepath.Join(served, "EXECUTE.KELF")); err != nil {
		return "", err
	}
	return fmt.Sprintf(" launcher=%s:%d", hex.EncodeToString(h.Sum(nil)), n), nil
}

func (o *Options) manifest(entries []entry, launcher string) []byte {
	var b strings.Builder
	auto := 0
	if o.Auto {
		auto = 1
	}
	fmt.Fprintf(&b, "udpfsd-manifest 1%s auto=%d\n", launcher, auto)
	for _, e := range entries {
		if e.Err != "" {
			fmt.Fprintf(&b, "%s\tinvalid:%s\t-\t-\t%d\t-\t0\t-\t-\n", e.Path, Sanitize(e.Err, 40), e.Size)
			continue
		}
		disc, jkt, cfg := "CD", "-", "-"
		if e.Info.DVD {
			disc = "DVD"
		}
		if e.Jacket {
			jkt = "jkt/" + e.Info.ID + ".png"
		}
		if e.CFG {
			cfg = "/CFG/" + e.Info.ID + ".cfg"
		}
		fmt.Fprintf(&b, "%s\tok\t%s\t%s\t%d\t%s\t%d\t%s\t%s\n", e.Path, e.Info.ID,
			Sanitize(e.Title, 63), e.Info.Bytes, disc, e.Info.Layer1, jkt, cfg)
	}
	return []byte(b.String())
}

// Run scans, prepares jackets, copies the launcher and writes the
// manifest (once after probing, again after downloads).
func Run(o Options) (Result, error) {
	served := ServedDir(o.CacheDir)
	if err := os.MkdirAll(filepath.Join(served, "jkt"), 0755); err != nil {
		return Result{}, err
	}
	var list map[string]string
	if o.Gamelist != "" {
		var err error
		if list, err = LoadGameList(o.Gamelist); err != nil {
			return Result{}, err
		}
	}
	cacheFile := filepath.Join(o.CacheDir, "scan.json")
	cached := map[string]entry{}
	if b, err := os.ReadFile(cacheFile); err == nil {
		json.Unmarshal(b, &cached)
	}
	entries := o.collect()
	res := Result{}
	for i := range entries {
		e := &entries[i]
		if c, ok := cached[e.Host]; ok && c.Size == e.Size && c.ModTime == e.ModTime {
			e.Err, e.Info = c.Err, c.Info
		} else {
			probe(e)
		}
		if e.Err != "" {
			res.Invalid++
			continue
		}
		res.Games++
		e.Title = Title(e.Info.ID, o.CFGDir, list, e.Host)
		if o.CFGDir != "" {
			if _, err := os.Stat(filepath.Join(o.CFGDir, e.Info.ID+".cfg")); err == nil {
				e.CFG = true
			}
		}
		jkt := filepath.Join(served, "jkt", e.Info.ID+".png")
		if src := FindArt(o.ARTDir, e.Info.ID, e.Host); src != "" {
			if fresh(jkt, src) || MakeJacket(src, jkt) == nil {
				e.Jacket = true
			} else {
				log.Printf("prep: jacket for %s from %s failed", e.Info.ID, src)
			}
		} else if _, err := os.Stat(jkt); err == nil {
			e.Jacket = true // downloaded earlier
		}
	}
	newCache := map[string]entry{}
	for _, e := range entries {
		newCache[e.Host] = entry{Size: e.Size, ModTime: e.ModTime, Err: e.Err, Info: e.Info}
	}
	if b, err := json.Marshal(newCache); err == nil {
		writeAtomic(cacheFile, b)
	}
	launcher := ""
	if o.Launcher != "" {
		if l, err := copyLauncher(o.Launcher, served); err == nil {
			launcher = l
		} else {
			log.Printf("prep: OPL-Launcher not served: %v", err)
		}
	}
	if err := writeAtomic(filepath.Join(served, "manifest.txt"), o.manifest(entries, launcher)); err != nil {
		return res, err
	}
	if !o.Download || o.Downloader == nil {
		return res, nil
	}
	// Phase 2: download missing covers, 4 at a time, then rewrite.
	var wg sync.WaitGroup
	sem := make(chan struct{}, 4)
	done := map[string]bool{}
	for i := range entries {
		e := &entries[i]
		if e.Err != "" || e.Jacket || done[e.Info.ID] {
			continue
		}
		done[e.Info.ID] = true
		wg.Add(1)
		sem <- struct{}{}
		go func(id string) {
			defer func() { <-sem; wg.Done() }()
			if src, err := o.Downloader.Fetch(id); err == nil {
				if err := MakeJacket(src, filepath.Join(served, "jkt", id+".png")); err != nil {
					log.Printf("prep: cover %s: %v", id, err)
				}
			}
		}(e.Info.ID)
	}
	wg.Wait()
	for i := range entries {
		e := &entries[i]
		if e.Err == "" && !e.Jacket {
			if _, err := os.Stat(filepath.Join(served, "jkt", e.Info.ID+".png")); err == nil {
				e.Jacket = true
			}
		}
	}
	return res, writeAtomic(filepath.Join(served, "manifest.txt"), o.manifest(entries, launcher))
}

// fresh reports whether dst exists and is newer than src.
func fresh(dst, src string) bool {
	d, err1 := os.Stat(dst)
	s, err2 := os.Stat(src)
	return err1 == nil && err2 == nil && d.ModTime().After(s.ModTime())
}
```

Note for the test file: in `TestRunFullScan` the first manifest entry contains a literal tab in its file name; replace that fixture name with `"Alien Hominid.iso"` (space) and compare `lines[0]` to `want[0]` like the others — tabs in host file names are out of scope. Use this loop instead of the special-casing:

```go
	for i, l := range lines[:3] {
		if l != want[i] {
			t.Fatalf("line %d:\n%q\nwant\n%q", i, l, want[i])
		}
	}
```

(and write the fixture as `os.WriteFile(filepath.Join(dvd, "Alien Hominid.iso"), other, 0644)`).

- [ ] **Step 4: Run tests** — Expected: `ok .../internal/prep`.

- [ ] **Step 5: Save and commit** — `bash tools/udpfsd-patch.sh save 0002-game-prep.patch && git add patches/udpfsd/0002-game-prep.patch && git commit -m "udpfsd: scanner, scan cache, manifest, launcher copy (patch 0002)"`

---

### Task 7: Wire udpfsd main, build script, dist

**Files:**
- Modify (dev tree): `cmd/udpfsd/main.go`
- Create (dev tree): `cmd/udpfsd/main_test.go`
- Modify: `tools/build-udpfsd.sh`, `Makefile` (dist), `tools/write-manifest.sh`
- Create: `docs/udpfsd-example/udpfsd.cfg`

**Interfaces:**
- Consumes: `config.Load/Set/Defaults/HasGameFolders` (Task 1), `fs.WithMount/PrepMountName/InstallDirName` (Task 2), `prep.Run/Options/Folder/ServedDir/Downloader/CoverURL` (Tasks 5–6).
- Produces: `udpfsd` with `-config`, `-no-prep`, `-no-download`; `buildPlan(cfg config.Config) (mounts []prep.Folder)` in main (tested).

- [ ] **Step 1: Write the failing test** — `cmd/udpfsd/main_test.go`

```go
package main

import (
	"testing"

	"github.com/pcm720/udpfsd/internal/config"
)

func TestGameFoldersFromConfig(t *testing.T) {
	c := config.Defaults()
	c.DVD, c.CD, c.Games, c.Install = "/d", "/c", "", "/i"
	got := gameFolders(c)
	want := []string{"DVD=/d", "CD=/c", "INSTALL=/i"}
	if len(got) != len(want) {
		t.Fatalf("%v", got)
	}
	for i, f := range got {
		if f.Mount+"="+f.Dir != want[i] {
			t.Fatalf("%v", got)
		}
	}
}

func TestPick(t *testing.T) {
	if pick(true, "flag", "env", "cfg") != "flag" || pick(false, "", "env", "cfg") != "env" ||
		pick(false, "", "", "cfg") != "cfg" || pick(false, "def", "", "") != "def" {
		t.Fatal("precedence")
	}
}
```

- [ ] **Step 2: Run to verify it fails** — Expected: FAIL, `undefined: gameFolders`, `undefined: pick`.

- [ ] **Step 3: Implement** — `cmd/udpfsd/main.go`: add imports `"path/filepath"`, `"github.com/pcm720/udpfsd/internal/config"`, `"github.com/pcm720/udpfsd/internal/prep"`; add flags next to the others:

```go
	configPath   = flag.String("config", "", "Configuration file (default: udpfsd.cfg next to the udpfsd binary)\nEnvironment variable: CONFIG")
	noPrep       = flag.Bool("no-prep", false, "Do not prepare game metadata/jackets/manifest at startup\nEnvironment variable: NO_PREP")
	noDownload   = flag.Bool("no-download", false, "Do not download missing covers\nEnvironment variable: NO_DOWNLOAD")
```

Add helpers at the end of the file:

```go
// pick applies flag > environment > config > default precedence.
func pick(flagSet bool, flagVal, envVal, cfgVal string) string {
	switch {
	case flagSet:
		return flagVal
	case envVal != "":
		return envVal
	case cfgVal != "":
		return cfgVal
	}
	return flagVal
}

func gameFolders(c config.Config) []prep.Folder {
	var out []prep.Folder
	for _, f := range []prep.Folder{{"DVD", c.DVD}, {"CD", c.CD}, {"GAMES", c.Games}, {fs.InstallDirName, c.Install}} {
		if f.Dir != "" {
			out = append(out, f)
		}
	}
	return out
}

func exeDir() string {
	p, err := os.Executable()
	if err != nil {
		return "."
	}
	if r, err := filepath.EvalSymlinks(p); err == nil {
		p = r
	}
	return filepath.Dir(p)
}

// loadConfig reads udpfsd.cfg (explicit -config/CONFIG must exist).
func loadConfig() config.Config {
	base := exeDir()
	path := pick(false, *configPath, os.Getenv("CONFIG"), "")
	explicit := path != ""
	if !explicit {
		path = filepath.Join(base, "udpfsd.cfg")
	}
	if _, err := os.Stat(path); err != nil {
		if explicit {
			log.Fatalf("config: %v", err)
		}
		c := config.Defaults()
		c.Cache = filepath.Join(base, c.Cache)
		c.OPLLauncher = filepath.Join(base, c.OPLLauncher)
		return c
	}
	c, err := config.Load(path, filepath.Dir(path))
	if err != nil {
		log.Fatalf("config: %v", err)
	}
	log.Printf("config: loaded %s", path)
	return c
}
```

In `main()`, replace the block from `flag.Parse()` through the `fs.NewBackend` call with:

```go
	flag.Parse()
	set := map[string]bool{}
	flag.Visit(func(f *flag.Flag) { set[f.Name] = true })
	loadEnvironment()
	cfg := loadConfig()

	// flag > environment (already applied by loadEnvironment) > udpfsd.cfg
	*root = pick(set["fsroot"] || *root != "", *root, "", cfg.FSRoot)
	*installDir = pick(set["install-dir"] || *installDir != "", *installDir, "", cfg.Install)
	*bindIP = pick(set["bind"] || *bindIP != "", *bindIP, "", cfg.Bind)
	if !set["port"] && os.Getenv("PORT") == "" {
		*port = cfg.Port
	}
	if !set["ro"] && os.Getenv("RO") == "" {
		*readOnly = cfg.ReadOnly
	}
	cfg.Install = *installDir

	if *path == "" && *root == "" && !cfg.HasGameFolders() {
		*root = defaultFsRoot
	}

	fsopts := []fs.BackendOptFunc{
		fs.WithFSRoot(*root),
		fs.WithBlockDevice(*path),
		fs.WithSectorSize(*sectorSize),
		fs.WithCompressionCacheSize(*compressionCacheSize),
	}
	for _, f := range gameFolders(cfg) {
		fsopts = append(fsopts, fs.WithMount(f.Mount, f.Dir))
	}
	if cfg.ART != "" {
		fsopts = append(fsopts, fs.WithMount("ART", cfg.ART))
	}
	if cfg.CFG != "" {
		fsopts = append(fsopts, fs.WithMount("CFG", cfg.CFG))
	}
	prepOn := !*noPrep && os.Getenv("NO_PREP") == "" && len(gameFolders(cfg)) > 0
	if prepOn {
		if err := os.MkdirAll(prep.ServedDir(cfg.Cache), 0755); err != nil {
			log.Fatalf("cache: %v", err)
		}
		fsopts = append(fsopts, fs.WithMount(fs.PrepMountName, prep.ServedDir(cfg.Cache)))
	}
	if *readOnly {
		fsopts = append(fsopts, fs.WithReadOnly())
	}
	if !*disableCompression {
		fsopts = append(fsopts, fs.WithCompression())
	}
	fsbackend, err := fs.NewBackend(fsopts...)
	if err != nil {
		log.Printf("failed to initialize filesystem: %v\n\n", err)
		flag.Usage()
		os.Exit(1)
	}
	if prepOn {
		launcher := cfg.OPLLauncher
		if _, err := os.Stat(launcher); err != nil {
			log.Printf("prep: no OPL-Launcher at %s; the PS2 will use its embedded copy", launcher)
			launcher = ""
		}
		o := prep.Options{Folders: gameFolders(cfg), CFGDir: cfg.CFG, ARTDir: cfg.ART,
			Gamelist: cfg.Gamelist, CacheDir: cfg.Cache, Launcher: launcher, Auto: cfg.AutoInstall,
			Download: cfg.DownloadCovers && !*noDownload && os.Getenv("NO_DOWNLOAD") == "",
			Downloader: &prep.Downloader{BaseURL: prep.CoverURL, CacheDir: cfg.Cache}}
		go func() {
			res, err := prep.Run(o)
			if err != nil {
				log.Printf("prep: %v", err)
				return
			}
			log.Printf("prep: %d games ready, %d invalid images", res.Games, res.Invalid)
		}()
	}
```

Update the usage line to: `fmt.Fprintf(os.Stderr, "\nGame folders and options can be set in udpfsd.cfg next to the binary.\n")`.

- [ ] **Step 4: Run Go tests** — `bash tools/udpfsd-patch.sh test` — Expected: all `ok` including `cmd/udpfsd`.

- [ ] **Step 5: Save the patch** — `bash tools/udpfsd-patch.sh save 0002-game-prep.patch`

- [ ] **Step 6: Config template** — `docs/udpfsd-example/udpfsd.cfg`

```ini
# udpfsd.cfg - read from the folder that contains the udpfsd binary.
# Relative paths are relative to that folder. Remove a line to disable it.
# Command-line flags and environment variables override these values.

# Game folders (served read-only to the PS2 as /DVD, /CD, /GAMES, /INSTALL)
dvd      = F:\ps2\PFS-BatchKit-Manager\DVD
cd       = F:\ps2\PFS-BatchKit-Manager\CD
#games   = F:\ps2\Games
#install = F:\ps2\ToInstall

# OPL per-game configs and art (CFG\<ID>.cfg, ART\<ID>_COV.jpg ...)
cfg      = F:\ps2\PFS-BatchKit-Manager\CFG
art      = F:\ps2\PFS-BatchKit-Manager\ART

# ID -> name reference list (e.g. PFS-BatchKit-Manager's GameListPS2.txt)
gamelist = GameListPS2.txt

# Prepared jackets, manifest and scan cache
cache    = udpfsd-cache

# Download covers missing from ART (xlenore/ps2-covers on GitHub)
download_covers = yes

# OPL-Launcher used for every new XMB game channel
opl_launcher = opl-launcher-EXECUTE.KELF

# Install every new game automatically when the installer starts
auto_install = yes

read_only = yes
#port = 62966
#bind = 192.168.1.100
```

- [ ] **Step 7: Ship it next to the server** — in `tools/build-udpfsd.sh` after the two `cp` lines of the build branch add nothing (binaries only). In the top-level `Makefile` `dist:` recipe, replace `cp $(UDPFSD_BIN) $(DIST)/udpfsd/` with:

```make
	cp $(UDPFSD_BIN) $(OPL_KELF) docs/udpfsd-example/udpfsd.cfg $(DIST)/udpfsd/
```

In `tools/write-manifest.sh` extend the `sha256sum` list with `udpfsd/opl-launcher-EXECUTE.KELF`. In `test/host/test_build_graph.sh` add `udpfsd/opl-launcher-EXECUTE.KELF udpfsd/udpfsd.cfg` to the `dist_contents` file list.

- [ ] **Step 8: Build and verify** — Run: `make udpfsd` — Expected: Go tests `ok` for every package; binaries built. Then:

```bash
mkdir -p /tmp/u/DVD && cp build/udpfsd/udpfsd-linux-amd64 /tmp/u/ && printf 'dvd = DVD\nauto_install = yes\n' > /tmp/u/udpfsd.cfg
timeout 3 /tmp/u/udpfsd-linux-amd64; ls /tmp/u/udpfsd-cache/served
```

Expected: log `config: loaded /tmp/u/udpfsd.cfg`, `mounted /tmp/u/DVD as /DVD`, `prep: 0 games ready`; `served` contains `manifest.txt` with header `udpfsd-manifest 1 auto=1`.

- [ ] **Step 9: Commit**

```bash
git add patches/udpfsd/0002-game-prep.patch docs/udpfsd-example/udpfsd.cfg Makefile tools/write-manifest.sh test/host/test_build_graph.sh
git commit -m "udpfsd: udpfsd.cfg, mounts and background game prep wired into main; ship cfg + launcher"
```

---

### Task 8: PS2 manifest parser

**Files:** Create `src/manifest.h`, `src/manifest.c`, `test/host/test_manifest.c`; Modify `test/host/Makefile`, `Makefile.ee`

**Interfaces:**
- Produces:

```c
#define MANIFEST_MAX 512
#define MANIFEST_DIR "udpfs:/.udpfsd"
typedef struct {
  char path[SOURCE_PATH_MAX]; /* client path, starts with '/' */
  int ok;
  char reason[48];
  char id[16];
  char title[64];
  uint64_t bytes;
  int dvd;
  uint32_t layer1;
  char jacket[64]; /* relative to MANIFEST_DIR, "" if none */
  char cfg[64];    /* client path, "" if none */
} manifest_entry_t;
typedef struct {
  int version;
  int has_launcher;
  char launcher_sha[65];
  uint32_t launcher_size;
  int auto_install;
  int n, n_bad;
  manifest_entry_t e[MANIFEST_MAX];
} manifest_t;
int manifest_parse(const char *text, size_t len, manifest_t *m); /* 0 or -1 (bad header) */
const manifest_entry_t *manifest_find_id(const manifest_t *m, const char *id);
const manifest_entry_t *manifest_find_path(const manifest_t *m, const char *udpfs_path);
#ifdef _EE
extern manifest_t g_manifest;
extern int g_manifest_loaded;
int manifest_load(void); /* reads MANIFEST_DIR/manifest.txt; sets g_manifest_loaded */
#endif
```

- [ ] **Step 1: Write the failing test** — `test/host/test_manifest.c`

```c
#include <stdlib.h>

#include "../../src/manifest.h"
#include "test.h"

static manifest_t m;

static const char GOOD[] =
    "udpfsd-manifest 1 launcher=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef:1561728 auto=1\n"
    "/DVD/GT4.iso\tok\tSLUS_203.12\tGran Turismo 4\t8547991552\tDVD\t1234\tjkt/SLUS_203.12.png\t/CFG/SLUS_203.12.cfg\n"
    "/CD/a.zso.iso\tok\tSLUS_210.90\tAlien Hominid\t2048000\tCD\t0\t-\t-\textra-future-column\r\n"
    "/DVD/broken.iso\tinvalid:not an ISO9660 image\t-\t-\t81920\t-\t0\t-\t-\n";

TEST(manifest_parses_header_and_entries) {
  CHECK_EQ_INT(manifest_parse(GOOD, strlen(GOOD), &m), 0);
  CHECK_EQ_INT(m.version, 1);
  CHECK(m.has_launcher);
  CHECK_EQ_INT(m.launcher_size, 1561728);
  CHECK_EQ_INT(strlen(m.launcher_sha), 64);
  CHECK(m.auto_install);
  CHECK_EQ_INT(m.n, 3);
  CHECK_EQ_INT(m.n_bad, 0);
  CHECK_STR(m.e[0].path, "/DVD/GT4.iso");
  CHECK(m.e[0].ok && m.e[0].dvd);
  CHECK_STR(m.e[0].id, "SLUS_203.12");
  CHECK_STR(m.e[0].title, "Gran Turismo 4");
  CHECK_EQ_U64(m.e[0].bytes, 8547991552ull);
  CHECK_EQ_INT(m.e[0].layer1, 1234);
  CHECK_STR(m.e[0].jacket, "jkt/SLUS_203.12.png");
  CHECK_STR(m.e[0].cfg, "/CFG/SLUS_203.12.cfg");
  CHECK(!m.e[1].dvd);
  CHECK_STR(m.e[1].jacket, "");
  CHECK_STR(m.e[1].cfg, "");
  CHECK(!m.e[2].ok);
  CHECK_STR(m.e[2].reason, "not an ISO9660 image");
}

TEST(manifest_header_variants) {
  const char *t = "udpfsd-manifest 1 auto=0\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK(!m.has_launcher && !m.auto_install && m.n == 0);
  t = "udpfsd-manifest 2 auto=1\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), -1);
  t = "garbage\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), -1);
  t = "udpfsd-manifest 1 launcher=xyz:12 auto=1\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK(!m.has_launcher); /* malformed hash: ignored, never trusted */
  CHECK_EQ_INT(manifest_parse("", 0, &m), -1);
}

TEST(manifest_rejects_bad_lines) {
  const char *t =
      "udpfsd-manifest 1 auto=0\n"
      "/DVD/a.iso\tok\tSLUS_203.12\tA\n"                                      /* too few */
      "relative.iso\tok\tSLUS_203.12\tA\t1\tDVD\t0\t-\t-\n"                   /* no '/' */
      "/DVD/b.iso\tok\tBADID\tB\t1\tDVD\t0\t-\t-\n"                           /* id */
      "/DVD/c.iso\tok\tSLUS_203.12\tC\t12x\tDVD\t0\t-\t-\n"                   /* bytes */
      "/DVD/d.iso\tok\tSLUS_203.12\tD\t1\tBD\t0\t-\t-\n"                      /* disc */
      "/DVD/e.iso\tok\tSLUS_203.12\tE\t1\tDVD\t0\t../x.png\t-\n"              /* jacket escape */
      "/DVD/f.iso\tok\tSLUS_203.12\tF\t2048\tDVD\t0\t-\t-\n";                 /* good */
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK_EQ_INT(m.n, 1);
  CHECK_EQ_INT(m.n_bad, 6);
  CHECK_STR(m.e[0].path, "/DVD/f.iso");
}

TEST(manifest_long_title_truncated_safely) {
  char t[600];
  snprintf(t, sizeof(t), "udpfsd-manifest 1 auto=0\n/DVD/a.iso\tok\tSLUS_203.12\t%0100d\t2048\tDVD\t0\t-\t-\n", 0);
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  CHECK_EQ_INT(m.n, 1);
  CHECK_EQ_INT(strlen(m.e[0].title), 63);
}

TEST(manifest_lookups) {
  manifest_parse(GOOD, strlen(GOOD), &m);
  CHECK(manifest_find_id(&m, "SLUS_210.90") == &m.e[1]);
  CHECK(manifest_find_id(&m, "SLUS_999.99") == NULL);
  CHECK(manifest_find_path(&m, "udpfs:/DVD/GT4.iso") == &m.e[0]);
  CHECK(manifest_find_path(&m, "/DVD/GT4.iso") == &m.e[0]);
  CHECK(manifest_find_path(&m, "udpfs:/DVD/broken.iso") == &m.e[2]);
  CHECK(manifest_find_path(&m, "udpfs:/nope.iso") == NULL);
}
```

- [ ] **Step 2: Add sources to builds and run (RED)** — `test/host/Makefile` `LIB_SRCS` append `manifest.c`; `Makefile.ee` `SRCS` append `manifest.c`. Create a stub `src/manifest.c` containing only `#include "manifest.h"` plus the header above. Run: `make -C test/host` — Expected: link failure `undefined reference to manifest_parse`.

- [ ] **Step 3: Implement** — `src/manifest.c`

```c
#include <stdio.h>
#include <string.h>

#include "manifest.h"
#include "partname.h"
#include "util.h"

static int is_hex64(const char *s, size_t n) {
  if (n != 64)
    return 0;
  for (size_t i = 0; i < n; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return 0;
  return 1;
}

static int parse_header(const char *line, manifest_t *m) {
  if (strncmp(line, "udpfsd-manifest ", 16) != 0)
    return -1;
  const char *p = line + 16;
  if (p[0] != '1' || (p[1] != 0 && p[1] != ' '))
    return -1;
  m->version = 1;
  for (p++; *p;) {
    while (*p == ' ')
      p++;
    const char *end = strchr(p, ' ');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if (n > 9 && !strncmp(p, "launcher=", 9)) {
      const char *colon = memchr(p + 9, ':', n - 9);
      uint64_t sz;
      char num[24];
      if (colon && is_hex64(p + 9, (size_t)(colon - p - 9))) {
        size_t nl = n - (size_t)(colon + 1 - p);
        if (nl > 0 && nl < sizeof(num)) {
          memcpy(num, colon + 1, nl);
          num[nl] = 0;
          if (parse_u64(num, &sz) == 0 && sz > 0 && sz < 0xFFFFFFFFull) {
            memcpy(m->launcher_sha, p + 9, 64);
            m->launcher_sha[64] = 0;
            m->launcher_size = (uint32_t)sz;
            m->has_launcher = 1;
          }
        }
      }
    } else if (n == 6 && !strncmp(p, "auto=", 5)) {
      m->auto_install = p[5] == '1';
    }
    p += n;
  }
  return 0;
}

/* Split `line` (modified in place) on tabs into at most max fields. */
static int split_tabs(char *line, char **f, int max) {
  int n = 0;
  f[n++] = line;
  for (char *p = line; *p && n < max; p++)
    if (*p == '\t') {
      *p = 0;
      f[n++] = p + 1;
    }
  for (char *p = f[n - 1]; *p; p++) /* drop future extra columns */
    if (*p == '\t') {
      *p = 0;
      break;
    }
  return n;
}

static int parse_entry(char *line, manifest_entry_t *e) {
  char *f[9];
  if (split_tabs(line, f, 9) != 9 || f[0][0] != '/' || strlen(f[0]) >= sizeof(e->path))
    return -1;
  memset(e, 0, sizeof(*e));
  str_copy(e->path, f[0], sizeof(e->path));
  if (parse_u64(f[4], &e->bytes) < 0)
    return -1;
  if (!strncmp(f[1], "invalid:", 8)) {
    str_copy(e->reason, f[1] + 8, sizeof(e->reason));
    return 0;
  }
  if (strcmp(f[1], "ok") || !boot_id_is_valid(f[2]))
    return -1;
  e->ok = 1;
  str_copy(e->id, f[2], sizeof(e->id));
  str_copy(e->title, f[3], sizeof(e->title));
  if (!strcmp(f[5], "DVD"))
    e->dvd = 1;
  else if (strcmp(f[5], "CD"))
    return -1;
  uint64_t l1;
  if (parse_u64(f[6], &l1) < 0 || l1 > 0xFFFFFFFFull)
    return -1;
  e->layer1 = (uint32_t)l1;
  if (strcmp(f[7], "-")) {
    if (strstr(f[7], "..") || f[7][0] == '/' || strlen(f[7]) >= sizeof(e->jacket))
      return -1;
    str_copy(e->jacket, f[7], sizeof(e->jacket));
  }
  if (strcmp(f[8], "-")) {
    if (strstr(f[8], "..") || f[8][0] != '/' || strlen(f[8]) >= sizeof(e->cfg))
      return -1;
    str_copy(e->cfg, f[8], sizeof(e->cfg));
  }
  return 0;
}

int manifest_parse(const char *text, size_t len, manifest_t *m) {
  memset(m, 0, sizeof(*m));
  static char line[1024];
  size_t pos = 0;
  int first = 1;
  while (pos < len) {
    size_t end = pos;
    while (end < len && text[end] != '\n')
      end++;
    size_t n = end - pos;
    if (n && text[pos + n - 1] == '\r')
      n--;
    if (n < sizeof(line)) {
      memcpy(line, text + pos, n);
      line[n] = 0;
      if (first) {
        if (parse_header(line, m) < 0)
          return -1;
        first = 0;
      } else if (n > 0) {
        if (m->n < MANIFEST_MAX && parse_entry(line, &m->e[m->n]) == 0)
          m->n++;
        else
          m->n_bad++;
      }
    } else {
      if (first)
        return -1;
      m->n_bad++;
    }
    pos = end + 1;
  }
  return first ? -1 : 0;
}

const manifest_entry_t *manifest_find_id(const manifest_t *m, const char *id) {
  for (int i = 0; i < m->n; i++)
    if (m->e[i].ok && !strcmp(m->e[i].id, id))
      return &m->e[i];
  return NULL;
}

const manifest_entry_t *manifest_find_path(const manifest_t *m, const char *p) {
  if (!strncmp(p, "udpfs:", 6))
    p += 6;
  for (int i = 0; i < m->n; i++)
    if (!strcmp(m->e[i].path, p))
      return &m->e[i];
  return NULL;
}

#ifdef _EE
#include <malloc.h>

#include "hdd_partitions.h"

manifest_t g_manifest;
int g_manifest_loaded;

int manifest_load(void) {
  void *buf = NULL;
  g_manifest_loaded = 0;
  int n = file_load(MANIFEST_DIR "/manifest.txt", &buf, 512 * 1024);
  if (n <= 0)
    return -1;
  int r = manifest_parse(buf, (size_t)n, &g_manifest);
  free(buf);
  g_manifest_loaded = r == 0;
  return r;
}
#endif
```

`src/manifest.h` contains exactly the Interfaces block above wrapped in `#ifndef PSXI_MANIFEST_H` / `#define PSXI_MANIFEST_H` with `#include <stddef.h>`, `#include <stdint.h>`, `#include "source.h"`.

- [ ] **Step 4: Run tests** — `make -C test/host` — Expected: all pass including 5 `manifest_*` tests.

- [ ] **Step 5: Commit** — `git add src/manifest.[ch] test/host/test_manifest.c test/host/Makefile Makefile.ee && git commit -m "PS2: udpfsd manifest parser"`

---

### Task 9: Server launcher selection and journal fields

**Files:** Create `src/server_assets.h`, `src/server_assets.c`, `test/host/test_server_assets.c`; Modify `src/transaction.h/.c`, `test/host/test_state.c`, `src/opl_launcher_payload.c`, `src/xmb_game_channel.c`, `src/diagnostics.c`, `test/host/Makefile`, `Makefile.ee`

**Interfaces:**
- Consumes: `manifest_t` (Task 8), `sha256_hex` (`src/sha256.h`), `kelf_looks_valid` (`src/hdl_header.h`).
- Produces:

```c
int launcher_copy_valid(const void *data, uint32_t size, const char *sha_hex, uint32_t want_size);
typedef enum { OPL_CFG_NONE = 0, OPL_CFG_COPY, OPL_CFG_KEEP } opl_cfg_action_t;
opl_cfg_action_t opl_cfg_decide(int offered, int dest_exists);
/* "pfs1:CFG/" for "+..." partitions, else "pfs1:OPL/CFG/" */
const char *opl_cfg_dir(const char *opl_partition);
```

Journal (`tx_journal_t`) gains `char launcher_source[24];` (`"server"`, `"embedded"`, or `""`) and `char opl_cfg[8];` (`"copied" "kept" "failed" "none"` or `""`); serialized as `launcher_source=` and `opl_cfg=`; included in `tx_journal_equal`.

- [ ] **Step 1: Write the failing tests** — `test/host/test_server_assets.c`

```c
#include "../../src/server_assets.h"
#include "../../src/sha256.h"
#include "test.h"

static uint8_t kelf[4096];

TEST(launcher_copy_requires_hash_size_and_kelf) {
  memset(kelf, 'K', sizeof(kelf));
  char hex[65];
  sha256_hex(kelf, sizeof(kelf), hex);
  CHECK(launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf)));
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf) - 1)); /* size mismatch */
  kelf[100] ^= 1;
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf))); /* stale/corrupt */
  kelf[100] ^= 1;
  memcpy(kelf, "\x7f" "ELF", 4);
  sha256_hex(kelf, sizeof(kelf), hex);
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), hex, sizeof(kelf))); /* plain ELF */
  CHECK(!launcher_copy_valid(NULL, 0, hex, 0));
  CHECK(!launcher_copy_valid(kelf, sizeof(kelf), "", sizeof(kelf)));
}

TEST(opl_cfg_never_overwrites) {
  CHECK_EQ_INT(opl_cfg_decide(1, 0), OPL_CFG_COPY);
  CHECK_EQ_INT(opl_cfg_decide(1, 1), OPL_CFG_KEEP);
  CHECK_EQ_INT(opl_cfg_decide(0, 0), OPL_CFG_NONE);
  CHECK_EQ_INT(opl_cfg_decide(0, 1), OPL_CFG_NONE);
}

TEST(opl_cfg_dir_follows_opl_prefix_rule) {
  CHECK_STR(opl_cfg_dir("+OPL"), "pfs1:CFG/");
  CHECK_STR(opl_cfg_dir("__common"), "pfs1:OPL/CFG/");
  CHECK_STR(opl_cfg_dir("PP.OPL"), "pfs1:OPL/CFG/");
}
```

Append to `test/host/test_state.c`:

```c
TEST(tx_launcher_and_opl_cfg_fields_roundtrip) {
  tx_journal_t j, k;
  verified_journal(&j);
  strcpy(j.launcher_source, "server");
  strcpy(j.opl_cfg, "copied");
  char buf[1200];
  CHECK(tx_serialize(&j, buf, sizeof(buf)) > 0);
  CHECK(strstr(buf, "launcher_source=server\n") != NULL);
  CHECK(strstr(buf, "opl_cfg=copied\n") != NULL);
  CHECK_EQ_INT(tx_parse(buf, &k), 0);
  CHECK(tx_journal_equal(&j, &k));
  strcpy(k.opl_cfg, "kept");
  CHECK(!tx_journal_equal(&j, &k));
}
```

- [ ] **Step 2: RED** — create `src/server_assets.c` with only `#include "server_assets.h"`; add `server_assets.c` to `test/host/Makefile` `LIB_SRCS` and `Makefile.ee` `SRCS`. Run `make -C test/host` — Expected: undefined references / missing struct members `launcher_source`, `opl_cfg`.

- [ ] **Step 3: Implement** — `src/server_assets.c`

```c
#include <string.h>

#include "hdl_header.h"
#include "server_assets.h"
#include "sha256.h"

int launcher_copy_valid(const void *data, uint32_t size, const char *sha_hex,
                        uint32_t want_size) {
  char hex[65];
  if (!data || size == 0 || size != want_size || !sha_hex || strlen(sha_hex) != 64 ||
      !kelf_looks_valid(data, size))
    return 0;
  sha256_hex(data, size, hex);
  return strcmp(hex, sha_hex) == 0;
}

opl_cfg_action_t opl_cfg_decide(int offered, int dest_exists) {
  if (!offered)
    return OPL_CFG_NONE;
  return dest_exists ? OPL_CFG_KEEP : OPL_CFG_COPY;
}

const char *opl_cfg_dir(const char *opl_partition) {
  /* OPL: gHDDPrefix is "pfs0:" for "+" partitions, else "pfs0:OPL/";
   * per-game configs live in <prefix>CFG/. The installer mounts the
   * OPL partition at pfs1:. */
  return opl_partition[0] == '+' ? "pfs1:CFG/" : "pfs1:OPL/CFG/";
}
```

`src/server_assets.h`: the Interfaces block above in include guards, with `#include <stdint.h>`.

`src/transaction.h`: in `tx_journal_t` after `last_error`, add:

```c
  char launcher_source[24]; /* "server" | "embedded" (channel KELF origin) */
  char opl_cfg[8];          /* "copied" | "kept" | "failed" | "none" */
```

`src/transaction.c`:
- `tx_serialize`: append `"launcher_source=%s\n" "opl_cfg=%s\n"` to the format string and `j->launcher_source, j->opl_cfg` to the arguments.
- `tx_parse`: add branches

```c
    else if (!strcmp(k, "launcher_source"))
      str_copy(out->launcher_source, v, sizeof(out->launcher_source));
    else if (!strcmp(k, "opl_cfg"))
      str_copy(out->opl_cfg, v, sizeof(out->opl_cfg));
```

- `tx_journal_equal`: append `&& !strcmp(a->launcher_source, b->launcher_source) && !strcmp(a->opl_cfg, b->opl_cfg)`.
- In the `_EE` section, change `static char io_buf[1024];` to `static char io_buf[1536];`.

`src/opl_launcher_payload.c`: add `#include "manifest.h"` and `#include "server_assets.h"`; at the top of `payload_opl_launcher` after `memset`, insert:

```c
  /* Server copy (udpfsd opl_launcher) when it matches the manifest. */
  if (udpfs_ok && g_manifest_loaded && g_manifest.has_launcher) {
    void *buf = NULL;
    int n = file_load(MANIFEST_DIR "/EXECUTE.KELF", &buf, KELF_MAX);
    if (n > 0 && launcher_copy_valid(buf, (uint32_t)n, g_manifest.launcher_sha,
                                     g_manifest.launcher_size)) {
      out->data = buf;
      out->size = (uint32_t)n;
      out->owned = 1;
      out->origin = "server";
      return ERR_OK;
    }
    free(buf);
  }
```

`src/xmb_game_channel.c`, in `build_channel` before `stage(ui, rep, STAGE_CREATING_CHANNEL);` add:

```c
  str_copy(j->launcher_source, !strcmp(kelf->origin, "server") ? "server" : "embedded",
           sizeof(j->launcher_source));
```

`src/diagnostics.c` `check_payloads`: replace the first `if (payload_opl_launcher(&k, 0) == ERR_OK) {` block with:

```c
  if (payload_opl_launcher(&k, g_app.net == NETWORK_READY) == ERR_OK) {
    if (!strcmp(k.origin, "server")) {
      char hex[65];
      sha256_hex(k.data, k.size, hex);
      line("PASS", "OPL-Launcher KELF from server, %lu bytes", (unsigned long)k.size);
      line(NULL, "     sha256 %.32s", hex);
      line(NULL, "            %.32s", hex + 32);
    } else {
      blob_check("OPL-Launcher KELF (embedded)", "opl_launcher_kelf", k.data, k.size);
    }
  } else {
```

- [ ] **Step 4: GREEN** — `make -C test/host` — Expected: all pass. Then `make dev` — Expected: rc=0.

- [ ] **Step 5: Commit** — `git add src/server_assets.[ch] src/transaction.[ch] src/opl_launcher_payload.c src/xmb_game_channel.c src/diagnostics.c test/host/test_server_assets.c test/host/test_state.c test/host/Makefile Makefile.ee && git commit -m "PS2: use the server's OPL-Launcher when it matches the manifest; journal launcher source"`

---

### Task 10: Manifest-driven plans, titles, jackets and lists

**Files:** Modify `src/xmb_game_channel.h/.c`, `src/flows.c`, `src/browser.c`, `src/network.c`, `src/batch.h/.c`; Test `test/host/test_batch.c`

**Interfaces:**
- Consumes: `manifest_t`, `manifest_find_id/path`, `g_manifest`, `manifest_load` (Task 8).
- Produces:

```c
/* Plan from a manifest entry without probing (bytes/disc/layer1/title from
 * the server). game_install() re-probes and adopts the PS2's own result. */
inst_err_t game_plan_from_manifest(const manifest_entry_t *m, game_plan_t *p);
/* batch.c, pure: fill entry fields from a manifest entry */
void batch_entry_from_manifest(batch_entry_t *e, const manifest_entry_t *m);
```

- [ ] **Step 1: Write the failing test** — append to `test/host/test_batch.c` (add `#include "../../src/manifest.h"` at the top)

```c
TEST(batch_from_manifest_duplicate_iso_and_zso) {
  static manifest_t m;
  const char *t =
      "udpfsd-manifest 1 auto=1\n"
      "/DVD/GTA SA.iso\tok\tSLUS_209.46\tGrand Theft Auto: San Andreas\t4697620480\tDVD\t0\t-\t-\n"
      "/DVD/GTA SA.zso.iso\tok\tSLUS_209.46\tGrand Theft Auto: San Andreas\t4697620480\tDVD\t0\t-\t-\n"
      "/DVD/bad.iso\tinvalid:no SYSTEM.CNF\t-\t-\t81920\t-\t0\t-\t-\n";
  CHECK_EQ_INT(manifest_parse(t, strlen(t), &m), 0);
  batch_entry_t e[3];
  for (int i = 0; i < 3; i++) {
    batch_entry_from_manifest(&e[i], &m.e[i]);
    if (m.e[i].ok)
      snprintf(e[i].hidden, sizeof(e[i].hidden), "__.SLUS-20946..GRAND_THEFT_AUTO");
  }
  CHECK_STR(e[0].path, "udpfs:/DVD/GTA SA.iso");
  CHECK_STR(e[1].name, "GTA SA.zso.iso");
  CHECK_EQ_INT(e[1].type, SRC_TYPE_ZSO);
  CHECK_STR(e[0].title, "Grand Theft Auto: San Andreas");
  CHECK_EQ_U64(e[0].bytes, 4697620480ull);
  CHECK_EQ_INT(e[2].probe_err, ERR_SOURCE_INVALID_ISO);
  batch_classify(e, 3);
  CHECK_EQ_INT(e[0].status, BATCH_ELIGIBLE);
  CHECK_EQ_INT(e[1].status, BATCH_DUPLICATE);
  CHECK_EQ_INT(e[2].status, BATCH_INVALID);
  CHECK_EQ_INT(batch_count_selected(e, 3), 1);
}
```

- [ ] **Step 2: RED** — `make -C test/host` — Expected: `undefined reference to batch_entry_from_manifest`.

- [ ] **Step 3: Implement**

`src/batch.h`: add `#include "manifest.h"` and declare `void batch_entry_from_manifest(batch_entry_t *e, const manifest_entry_t *m);`

`src/batch.c`: add `#include "util.h"` and

```c
void batch_entry_from_manifest(batch_entry_t *e, const manifest_entry_t *m) {
  memset(e, 0, sizeof(*e));
  snprintf(e->path, sizeof(e->path), "udpfs:%s", m->path);
  const char *slash = strrchr(m->path, '/');
  str_copy(e->name, slash ? slash + 1 : m->path, sizeof(e->name));
  e->type = source_classify(e->name);
  e->bytes = m->bytes;
  e->probe_err = m->ok ? ERR_OK : ERR_SOURCE_INVALID_ISO;
  str_copy(e->boot_id, m->id, sizeof(e->boot_id));
  str_copy(e->title, m->title, sizeof(e->title));
}
```

`src/xmb_game_channel.h`: add `#include "manifest.h"` and the `game_plan_from_manifest` prototype.

`src/xmb_game_channel.c`: add `#include "manifest.h"` and

```c
inst_err_t game_plan_from_manifest(const manifest_entry_t *m, game_plan_t *p) {
  memset(p, 0, sizeof(*p));
  if (!m->ok || m->bytes == 0 || m->bytes % ISO_SECTOR)
    return ERR_SOURCE_INVALID_ISO;
  snprintf(p->source_path, sizeof(p->source_path), "udpfs:%s", m->path);
  p->type = source_classify(p->source_path);
  str_copy(p->iso.boot_id, m->id, sizeof(p->iso.boot_id));
  boot_id_to_part_id(m->id, p->iso.part_id);
  p->iso.source_size = m->bytes;
  p->iso.sectors = (uint32_t)(m->bytes / ISO_SECTOR);
  p->iso.disc_type = m->dvd ? DISC_TYPE_DVD : DISC_TYPE_CD;
  p->iso.layer1_start = m->layer1;
  if (game_plan_set_title(p, m->title))
    return ERR_INVALID_ARG;
  uint32_t max_mb = 0;
  if (hdd_space_mb(NULL, NULL, &max_mb) < 0)
    return ERR_HDD_MISSING;
  return hdl_plan_alloc(m->bytes, max_mb, &p->alloc);
}
```

In `game_plan_build`, after `default_display_title(&p->iso, path, title, sizeof(title));` insert:

```c
  const manifest_entry_t *me = g_manifest_loaded ? manifest_find_path(&g_manifest, path) : NULL;
  if (me && me->ok && !strcmp(me->id, p->iso.boot_id) && me->title[0])
    str_copy(title, me->title, sizeof(title));
```

In `game_install`, after the `if (strcmp(again.boot_id, p->iso.boot_id) || again.sectors != p->iso.sectors) { ... }` block insert:

```c
  /* The PS2's own probe is authoritative for everything the HDL header
   * and the read-back verification use (volume ID, PVD size, disc type,
   * layer break); the manifest only supplied the plan. */
  p->iso = again;
```

In `load_jacket`, before `snprintf(path, sizeof(path), "udpfs:/ART/%s.png", boot_id);` insert:

```c
    const manifest_entry_t *me = g_manifest_loaded ? manifest_find_id(&g_manifest, boot_id) : NULL;
    if (me && me->jacket[0]) {
      char mp[SOURCE_PATH_MAX];
      void *buf = NULL;
      snprintf(mp, sizeof(mp), MANIFEST_DIR "/%s", me->jacket);
      int n = file_load(mp, &buf, JACKET_MAX);
      if (n > 0 && png_basic_valid(buf, (uint32_t)n)) {
        *data = buf;
        *size = (uint32_t)n;
        *owned = buf;
        return;
      }
      free(buf);
    }
```

`src/network.c`: add `#include "manifest.h"`; at the end of `network_start()` add `if (g_app.net == NETWORK_READY) manifest_load();`

`src/browser.c` `list_dir`: after the `.`/`..` skip add `if (de.name[0] == '.') continue; /* .udpfsd etc. */`.

`src/flows.c` `flow_batch_install`: replace

```c
  ui_header("Install All Games", "Reading " BATCH_DIR " ...");
  int n = 0;
  batch_collect(BATCH_DIR, 1, &n);
```

with

```c
  ui_header("Install All Games", "Reading the server's game list ...");
  int n = batch_load_entries();
```

and replace the following `ui_header(... "Checking images ...")` + `batch_probe(n);` with `batch_finish_entries(n);`. Add above `flow_batch_install`:

```c
/* Entries from the server manifest (all game folders), else a probe of
 * udpfs:/INSTALL as before. */
static int batch_load_entries(void) {
  int n = 0;
  manifest_load();
  if (g_manifest_loaded) {
    for (int i = 0; i < g_manifest.n && n < BATCH_MAX; i++, n++) {
      batch_entry_from_manifest(&batch[n], &g_manifest.e[i]);
      if (g_manifest.e[i].ok)
        batch[n].probe_err = game_plan_from_manifest(&g_manifest.e[i], &batch_plans[n]);
    }
    return n;
  }
  batch_collect(BATCH_DIR, 1, &n);
  return -n - 1; /* negative: entries still need probing */
}

static void batch_finish_entries(int n) {
  if (n < 0) {
    ui_header("Install All Games", "Checking images (ISO9660 + SYSTEM.CNF) ...");
    batch_probe(-n - 1);
    return;
  }
  for (int i = 0; i < n; i++) {
    batch_entry_t *e = &batch[i];
    game_plan_t *p = &batch_plans[i];
    if (e->probe_err == ERR_OK) {
      str_copy(e->visible, p->visible, sizeof(e->visible));
      str_copy(e->hidden, p->hidden, sizeof(e->hidden));
      e->alloc_mb = p->alloc.total_mb;
      pair_facts_t f;
      ui_at(4, " Checking HDD %d/%d", i + 1, n);
      game_pair_facts(p->visible, p->hidden, &f);
      e->pair = pair_classify(&f);
    }
  }
  batch_classify(batch, n);
}
```

and after `batch_finish_entries(n);` normalize `if (n < 0) n = -n - 1;` before `if (n == 0)`. Change the empty-list message to mention both sources: `"No games found. Configure game folders in udpfsd.cfg next to\nthe server, or put games in its install folder."`

- [ ] **Step 4: GREEN** — `make -C test/host` — Expected: all pass. `make dev` — Expected: rc=0.

- [ ] **Step 5: Commit** — `git add -A src test/host && git commit -m "PS2: manifest-driven Install All, titles and jackets from the server"`

---

### Task 11: Copy the OPL cfg after install

**Files:** Modify `src/xmb_game_channel.c/.h`, `src/flows.c` (summary column), `src/batch.c` (summary)

**Interfaces:**
- Consumes: `opl_cfg_decide`, `opl_cfg_dir` (Task 9), `opl_check_runtime` (`src/opl_dependency.h`), `pfs_mount/pfs_umount/file_load/file_write_all/file_size` (`src/hdd_partitions.h`), `manifest_find_id` (Task 8).
- Produces: `install_report_t` gains `const char *opl_cfg;` (`"copied" "kept" "failed" "none"`); `static const char *copy_opl_cfg(const char *boot_id)` in `xmb_game_channel.c`.

- [ ] **Step 1: Test** — decision logic is covered by `opl_cfg_never_overwrites` / `opl_cfg_dir_follows_opl_prefix_rule` (Task 9). Add to `test/host/test_batch.c` a summary check:

```c
TEST(batch_summary_mentions_opl_cfg_failure) {
  batch_entry_t e[1];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  batch_classify(e, 1);
  e[0].result = BATCH_DONE;
  e[0].opl_cfg = "failed";
  char sum[512];
  batch_summary(e, 1, sum, sizeof(sum));
  CHECK(strstr(sum, "OPL cfg not copied") != NULL);
}
```

- [ ] **Step 2: RED** — `make -C test/host` — Expected: compile error `no member named 'opl_cfg'`.

- [ ] **Step 3: Implement**

`src/batch.h` `batch_entry_t`: add `const char *opl_cfg; /* from install_report_t */`.
`src/batch.c` `batch_summary`: after the FAILED line handling add

```c
    if (e[i].result == BATCH_DONE && e[i].opl_cfg && !strcmp(e[i].opl_cfg, "failed") &&
        (size_t)off < outsz)
      off += snprintf(out + off, outsz - off, "          OPL cfg not copied\n");
```

`src/xmb_game_channel.h` `install_report_t`: add `const char *opl_cfg;`.

`src/xmb_game_channel.c`: add `#include "server_assets.h"`, `#include "opl_dependency.h"` (if missing) and

```c
/* Copy the server's CFG/<ID>.cfg to the OPL partition if OPL has none
 * yet. Best effort; returns "copied" | "kept" | "failed" | "none". */
static const char *copy_opl_cfg(const char *boot_id) {
  const manifest_entry_t *me = g_manifest_loaded ? manifest_find_id(&g_manifest, boot_id) : NULL;
  if (!me || !me->cfg[0])
    return "none";
  opl_runtime_t opl;
  int rc;
  if (opl_check_runtime(&opl, &rc) != ERR_OK)
    return "failed";
  char src[SOURCE_PATH_MAX], dst[96], tmp[100];
  snprintf(src, sizeof(src), "udpfs:%s", me->cfg);
  void *data = NULL;
  int n = file_load(src, &data, 64 * 1024);
  if (n <= 0)
    return "failed";
  const char *result = "failed";
  if (pfs_mount(PFS_WORK, opl.partition, FIO_MT_RDWR) == 0) {
    const char *dir = opl_cfg_dir(opl.partition);
    snprintf(dst, sizeof(dst), "%s%s.cfg", dir, boot_id);
    switch (opl_cfg_decide(1, file_size(dst) >= 0)) {
    case OPL_CFG_KEEP:
      result = "kept";
      break;
    case OPL_CFG_COPY: {
      char d[64];
      str_copy(d, dir, sizeof(d));
      d[strlen(d) - 1] = 0; /* drop trailing '/' */
      if (!strcmp(d, "pfs1:OPL/CFG"))
        fileXioMkdir("pfs1:OPL", 0777);
      fileXioMkdir(d, 0777);
      snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
      void *back = NULL;
      if (file_write_all(tmp, data, (uint32_t)n) == 0 &&
          file_load(tmp, &back, (uint32_t)n + 1) == n && !memcmp(back, data, (size_t)n) &&
          fileXioRename(tmp, dst) >= 0)
        result = "copied";
      else
        fileXioRemove(tmp);
      free(back);
      break;
    }
    case OPL_CFG_NONE:
      result = "none";
      break;
    }
    pfs_umount(PFS_WORK);
  }
  free(data);
  return result;
}
```

(add `#define NEWLIB_PORT_AWARE` + `#include <fileXio_rpc.h>` + `#include <io_common.h>` at the top of `xmb_game_channel.c` if not present).

In `build_channel`, in the success branch (where `rep->err = ERR_OK;`), replace it with:

```c
    rep->err = ERR_OK;
    rep->opl_cfg = copy_opl_cfg(j->startup_id);
    str_copy(j->opl_cfg, rep->opl_cfg, sizeof(j->opl_cfg));
    persist(j); /* best effort: the install is already complete */
```

`src/flows.c`: in the batch loop after `e->stage = install_stage_name(rep.stage);` add `e->opl_cfg = rep.opl_cfg;`. In `run_install`'s success message append a line: `"OPL settings:     %s\n"` with `rep.opl_cfg ? rep.opl_cfg : "none"` (enlarge `msg` to 800).

- [ ] **Step 4: GREEN** — `make -C test/host` and `make dev` — Expected: pass / rc=0.

- [ ] **Step 5: Commit** — `git add -A src test/host && git commit -m "PS2: copy the server's OPL per-game cfg after install (never overwrite)"`

---

### Task 12: Auto-install mode

**Files:** Modify `src/batch.h/.c`, `src/flows.c/.h`, `src/main.c`, `src/ui.h/.c`; Test `test/host/test_batch.c`

**Interfaces:**
- Consumes: Tasks 8–11; `installer_app_install` (`src/xmb_installer_app.h`), `opl_check_runtime`, `hdd_space_mb`.
- Produces: `BATCH_NO_SPACE` status; `int batch_auto_select(batch_entry_t *e, int n, uint64_t free_mb);` (selects eligible entries in order while cumulative need ≤ free_mb; others eligible → `BATCH_NO_SPACE`, unselected; returns count); `void flow_auto_install(void);`; `int ui_wait_button_timeout(int ms);` (0 on timeout).

- [ ] **Step 1: Write the failing test** — append to `test/host/test_batch.c`

```c
TEST(batch_auto_select_fits_free_space_in_order) {
  batch_entry_t e[4];
  e[0] = ent("A.iso", ERR_OK, "__.SLUS-20312..A", PAIR_NONE, 4096);
  e[1] = ent("B.iso", ERR_OK, "__.SLUS-20313..B", PAIR_NONE, 4096);
  e[2] = ent("C.iso", ERR_OK, "__.SLUS-20314..C", PAIR_NONE, 512);
  e[3] = ent("D.iso", ERR_OK, "__.SLUS-20315..D", PAIR_COMPLETE, 512);
  batch_classify(e, 4);
  /* 4096+128 fits, next 4224 does not, 512+128 still fits */
  CHECK_EQ_INT(batch_auto_select(e, 4, 4224 + 640), 2);
  CHECK(e[0].selected && !e[1].selected && e[2].selected && !e[3].selected);
  CHECK_EQ_INT(e[1].status, BATCH_NO_SPACE);
  CHECK_EQ_INT(e[3].status, BATCH_EXISTS);
  CHECK(strcmp(batch_status_label(BATCH_NO_SPACE), "no space") == 0);
  CHECK_EQ_INT(batch_auto_select(e, 4, 0), 0);
}
```

Also extend `batch_labels_nonempty` loop bound from `BATCH_TOO_BIG` to `BATCH_NO_SPACE`.

- [ ] **Step 2: RED** — `make -C test/host` — Expected: `BATCH_NO_SPACE` undeclared.

- [ ] **Step 3: Implement**

`src/batch.h`: add `BATCH_NO_SPACE, /* auto mode: does not fit the remaining free space */` after `BATCH_TOO_BIG` and declare `batch_auto_select`.

`src/batch.c`: label case `case BATCH_NO_SPACE: return "no space";` and

```c
int batch_auto_select(batch_entry_t *e, int n, uint64_t free_mb) {
  uint64_t used = 0;
  int count = 0;
  for (int i = 0; i < n; i++) {
    if (e[i].status == BATCH_NO_SPACE)
      e[i].status = BATCH_ELIGIBLE;
    e[i].selected = 0;
    if (e[i].status != BATCH_ELIGIBLE)
      continue;
    uint64_t need = (uint64_t)e[i].alloc_mb + 128;
    if (used + need > free_mb) {
      e[i].status = BATCH_NO_SPACE;
      continue;
    }
    used += need;
    e[i].selected = 1;
    count++;
  }
  return count;
}
```

`src/ui.h`/`src/ui.c`:

```c
/* Wait up to ms for a new button press; returns the mask or 0. Without a
 * pad it simply waits ms and returns 0. */
int ui_wait_button_timeout(int ms) {
  for (int t = 0; t < ms; t += 50) {
    int b = ui_poll_button();
    if (b)
      return b;
    ui_delay_ms(50);
  }
  return 0;
}
```

`src/flows.h`: declare `void flow_auto_install(void);` with comment "Fully automatic install of every new game listed by the server (udpfsd auto_install = yes). Returns only if cancelled or stopped; on completion it exits to the system menu."

`src/flows.c`: refactor the per-game loop of `flow_batch_install` into

```c
static void batch_run_selected(int n, int allow_without_opl) {
  int idx = 0, total = batch_count_selected(batch, n), stop = 0;
  for (int i = 0; i < n; i++) {
    batch_entry_t *e = &batch[i];
    if (!e->selected)
      continue;
    idx++;
    if (stop) {
      e->result = BATCH_SKIPPED;
      continue;
    }
    char title[48];
    snprintf(title, sizeof(title), "Install %d/%d", idx, total);
    progress_ctx_t ctx = {&batch_plans[i], STAGE_PREPARING};
    install_ui_t ui = {cb_stage, cb_progress, cb_abort, &ctx};
    install_report_t rep;
    draw_install_static(&batch_plans[i], title);
    ui_footer("Do not power off.");
    game_install(&batch_plans[i], allow_without_opl, &ui, &rep);
    e->err = rep.err;
    e->stage = install_stage_name(rep.stage);
    e->opl_cfg = rep.opl_cfg;
    e->result = rep.err == ERR_OK              ? BATCH_DONE
                : rep.data_installed_no_channel ? BATCH_DATA_ONLY
                                                : BATCH_FAILED;
    if (rep.err == ERR_USER_ABORT && idx < total)
      stop = ui_confirm("Install", "Game aborted. Stop the remaining games too?");
  }
}
```

and call `batch_run_selected(n, allow_without_opl);` where the loop was. Then add:

```c
static void auto_show(const char *title, const char *text, int ms) {
  ui_header(title, NULL);
  int row = 3;
  for (const char *p = text; *p && row < UI_ROWS - 2;) {
    const char *nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int)strlen(p);
    ui_at(row++, " %.*s", len > UI_COLS - 2 ? UI_COLS - 2 : len, p);
    p = nl ? nl + 1 : p + len;
  }
  ui_footer("[any button] continue");
  ui_wait_button_timeout(ms);
}

void flow_auto_install(void) {
  /* 1. Wait (bounded) for the server's manifest: its scan may still run. */
  for (int t = 0; !g_manifest_loaded && t < 12; t++) {
    ui_header("Auto-install", "Waiting for the server's game list ...");
    if (ui_wait_button_timeout(5000) & (UI_CIRCLE | UI_TRIANGLE))
      return;
    manifest_load();
  }
  if (!g_manifest_loaded || !g_manifest.auto_install)
    return;

  /* 2. Countdown; O/Triangle cancels, nothing needs to be pressed. */
  for (int s = 10; s > 0; s--) {
    char st[80];
    snprintf(st, sizeof(st), "Auto-installing new games in %d s - [O] cancel", s);
    ui_header("Auto-install", st);
    ui_at(4, " %d game image(s) listed by the server.", g_manifest.n);
    if (ui_wait_button_timeout(1000) & (UI_CIRCLE | UI_TRIANGLE))
      return;
  }

  /* 3. First run: create the installer partition (journal storage). */
  if (!g_app.app_mounted) {
    ui_header("Auto-install", "Creating PP.UDPFS-INSTALLER ...");
    selfinstall_report_t sr;
    installer_app_install(&sr);
    if (sr.err || !g_app.app_mounted) {
      char msg[200];
      snprintf(msg, sizeof(msg), "Could not create the installer partition:\n%s (%s)\n\n"
               "Auto-install stopped; nothing else was changed.", err_text(sr.err), err_name(sr.err));
      auto_show("Auto-install", msg, 15000);
      return;
    }
  }

  /* 4. OPL must be present: never create channel-less games here. */
  opl_runtime_t opl;
  int orc;
  if (opl_check_runtime(&opl, &orc) != ERR_OK) {
    char msg[300];
    snprintf(msg, sizeof(msg), "OPL runtime not found (looked for %s on hdd0:%s).\n\n"
             "Auto-install did not install anything. Install OPL, then\nrestart the installer.",
             opl.elf_path, opl.partition);
    auto_show("Auto-install", msg, 15000);
    return;
  }

  /* 5. New games that fit, one after another. */
  ui_header("Auto-install", "Checking the HDD ...");
  int n = batch_load_entries();
  batch_finish_entries(n);
  if (n < 0)
    n = -n - 1;
  uint32_t free_mb = 0;
  hdd_space_mb(NULL, &free_mb, NULL);
  if (batch_auto_select(batch, n, free_mb) == 0) {
    auto_show("Auto-install", "No new games to install.", 5000);
    LoadExecPS2("rom0:OSDSYS", 0, NULL);
    return;
  }
  batch_run_selected(n, 0);

  /* 6. Summary, then back to the XMB. */
  static char summary[4096];
  batch_summary(batch, n, summary, sizeof(summary));
  auto_show("Auto-install finished", summary, 15000);
  app_unmount();
  ui_pad_close();
  LoadExecPS2("rom0:OSDSYS", 0, NULL);
}
```

(add `#include "manifest.h"` and `#include "xmb_installer_app.h"` to `flows.c` if missing; `batch_load_entries`/`batch_finish_entries` from Task 10 must be defined above this function).

`src/main.c`: replace

```c
  app_boot();
  startup_notices();
```

with

```c
  app_boot();
  /* Fully automatic path: no prompts before or during the run. */
  if (g_app.iop.hdd_ok && g_app.hdd_state == ERR_OK && g_app.net == NETWORK_READY &&
      g_manifest_loaded && g_manifest.auto_install)
    flow_auto_install();
  startup_notices();
```

(add `#include "manifest.h"`).

- [ ] **Step 4: GREEN** — `make -C test/host` — Expected: all pass. `make dev` — Expected: rc=0.

- [ ] **Step 5: Commit** — `git add -A src test/host && git commit -m "PS2: auto-install mode (udpfsd auto_install = yes)"`

---

### Task 13: Documentation, checklist, full release build

**Files:** Modify `docs/udpfsd-example/README.txt`, `docs/INSTALL.md`, `docs/HARDWARE_TEST_CHECKLIST.md`, `KNOWN_LIMITATIONS.md`, `docs/BUILD.md`, `docs/PROVENANCE.md`

- [ ] **Step 1: Server guide** — in `docs/udpfsd-example/README.txt` replace the opening command examples and the `-install-dir` section with:

```text
Quick start
-----------
1. Put udpfsd.cfg next to the udpfsd binary (template in this folder)
   and set your folders: dvd, cd, games, install, cfg, art, gamelist.
2. Keep opl-launcher-EXECUTE.KELF next to the binary (shipped).
3. Start udpfsd with no arguments:
      udpfsd-windows-amd64.exe
   It scans every game, prepares titles, jackets and OPL configs into
   udpfsd-cache\, downloads missing covers (download_covers = yes), and
   serves everything read-only to the console.
4. With auto_install = yes, starting the installer on the DESR installs
   every new game automatically and returns to the XMB.

The console sees /DVD /CD /GAMES /INSTALL /ART /CFG and /.udpfsd
(manifest.txt, jkt/<ID>.png, EXECUTE.KELF). Flags still work and
override udpfsd.cfg: -config, -fsroot, -install-dir, -no-prep,
-no-download, -ro, -port, -bind.
```

- [ ] **Step 2: Install guide** — in `docs/INSTALL.md` add section `## 0b. Fully automatic installs` describing: configure `udpfsd.cfg` (`auto_install = yes`), start udpfsd, start the ELF; 10 s countdown (O cancels); first run creates `PP.UDPFS-INSTALLER`; OPL missing -> nothing installed; only new games that fit; summary 15 s; returns to the XMB.

- [ ] **Step 3: Checklist** — add to `docs/HARDWARE_TEST_CHECKLIST.md`:
  - Level 1 rows `P15 | udpfsd.cfg, mounts, prep (ISO/ZSO probe, titles, art, downloads, manifest, cache) | Go tests (make test-udpfsd) | PASS` and `P16 | PS2 manifest parser, server launcher check, OPL cfg decision, auto selection | host tests | PASS` (mark PASS only after Step 6 runs green).
  - Level 2 row `N10 | Install All lists games from all configured folders instantly with real titles | NOT RUN`.
  - Level 3 rows `D19 | Jackets from ART appear in the XMB | NOT RUN`, `D20 | OPL cfg copied; compatibility modes take effect; $VMC entry behaviour | NOT RUN`, `D21 | Server OPL-Launcher used for channels (Diagnostics shows "from server") | NOT RUN`, `D22 | Fresh console, no installer partition, ISO + ZSO configured, auto_install = yes: ELF started, no further input, both games boot from the XMB | NOT RUN`, `D23 | LoadExecPS2("rom0:OSDSYS") returns to the DESR XMB after auto-install | NOT RUN`.

- [ ] **Step 4: Limitations/BUILD/PROVENANCE** — `KNOWN_LIMITATIONS.md`: add bullets "Auto-install exits via `rom0:OSDSYS`; whether that lands in the DESR XMB is checklist D23", "Cover downloads are third-party images (xlenore/ps2-covers)", "The server's title sources are your CFG folder and GameListPS2.txt; no title database is bundled". `docs/BUILD.md`: describe `tools/udpfsd-patch.sh init|test|save` and that `make dist` ships `udpfsd.cfg` and `opl-launcher-EXECUTE.KELF` in `dist/udpfsd/`. `docs/PROVENANCE.md`: extend the udpfsd section with `patches/udpfsd/0002-game-prep.patch` (new packages `internal/config`, `internal/prep`; mounts replace `installdir.go`).

- [ ] **Step 5: Commit docs** — `git add -A docs KNOWN_LIMITATIONS.md && git commit -m "docs: udpfsd.cfg, game prep, auto-install"`

- [ ] **Step 6: Full verification**

Run: `make clean && PS2KEYS=/root/ps2keys/PS2KEYS.dat KELF_MODE=none make dist && make test-graph`
Expected: rc=0; host tests all pass (count printed), driver 14/14, kelf-sign 14/14, Go tests `ok` for every package, build graph all passed; `dist/udpfsd/` contains `udpfsd-windows-amd64.exe`, `udpfsd-linux-amd64`, `opl-launcher-EXECUTE.KELF`, `udpfsd.cfg`; `dist/SHA256SUMS` verifies.

Then smoke-test the server against the user's real folders (read-only, no network to the PS2 needed):

```bash
mkdir -p /tmp/srv && cp dist/udpfsd/* /tmp/srv/ && cp /mnt/f/ps2/PFS-BatchKit-Manager/GameListPS2.txt /tmp/srv/
printf 'dvd = /mnt/f/ps2/PFS-BatchKit-Manager/DVD\ncfg = /mnt/f/ps2/PFS-BatchKit-Manager/CFG\nart = /mnt/f/ps2/PFS-BatchKit-Manager/ART\ngamelist = GameListPS2.txt\ndownload_covers = no\nauto_install = yes\nread_only = yes\n' > /tmp/srv/udpfsd.cfg
timeout 300 /tmp/srv/udpfsd-linux-amd64 & sleep 240; head -20 /tmp/srv/udpfsd-cache/served/manifest.txt; ls /tmp/srv/udpfsd-cache/served/jkt | wc -l
```

Expected: header `udpfsd-manifest 1 launcher=<sha>:<bytes> auto=1`; 16 entries for the 16 DVD images, each `ok` with a real title; jackets present for games with ART.

- [ ] **Step 7: Commit checklist PASS marks** — `git add docs/HARDWARE_TEST_CHECKLIST.md && git commit -m "checklist: PC verification of game prep and auto-install"`
