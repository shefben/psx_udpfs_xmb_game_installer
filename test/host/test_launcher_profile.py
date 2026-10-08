#!/usr/bin/env python3
"""Verify the real payload resolver cannot use a generic server KELF on PSX1."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "manifest.h"
#include "opl_launcher_payload.h"
app_state_t g_app;
manifest_t g_manifest;
int g_manifest_loaded=1;
unsigned char psx1_launcher_kelf[2048]={1};
unsigned int size_psx1_launcher_kelf=2048;
static int loads;
int file_load(const char *p,void **out,uint32_t max) {
  (void)p; (void)max; loads++; *out=calloc(1,2048); return 2048;
}
int launcher_copy_valid(const void *p,uint32_t n,const char *sha,uint32_t expected) {
  (void)p;(void)n;(void)sha;(void)expected;return 1;
}
int kelf_looks_valid(const void *p,uint32_t n) { return p && n>=1024; }
int main(void) {
  payload_t p;
  g_manifest.has_launcher=1;
  g_app.settings.console=CONSOLE_PSX1;
#ifdef HAVE_EMBEDDED_PSX1_LAUNCHER
  assert(payload_opl_launcher(&p,1)==ERR_OK);
  assert(p.data==psx1_launcher_kelf && !p.owned && loads==0);
#else
  assert(payload_opl_launcher(&p,1)==ERR_KELF_MISSING && loads==0);
#endif
  payload_release(&p);
  g_app.settings.console=CONSOLE_UNKNOWN;
  assert(payload_opl_launcher(&p,1)==ERR_CONSOLE_UNKNOWN && loads==0);
  g_app.settings.console=CONSOLE_PSX2;
  assert(payload_opl_launcher(&p,1)==ERR_OK && loads==1 && p.owned);
  assert(!strcmp(p.origin,"server"));
  payload_release(&p);
}
'''
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'profile.c'
    path.write_text(HARNESS)
    for embedded in [False, True]:
        binary = Path(d) / ('embedded' if embedded else 'missing')
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-ffunction-sections', '-fdata-sections',
                        '-DVARIANT_DEV', '-D_EE', *(['-DHAVE_EMBEDDED_PSX1_LAUNCHER'] if embedded else []),
                        '-I' + str(ROOT / 'src'), str(path), str(ROOT / 'src/opl_launcher_payload.c'),
                        '-Wl,--gc-sections', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Production launcher profile tests passed (embedded and missing PSX1 payload)')
