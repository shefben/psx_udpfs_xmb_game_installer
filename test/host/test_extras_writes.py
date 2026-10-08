#!/usr/bin/env python3
"""Check production extras replacement preserves existing files on failure."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/extras_install.c').read_text()
functions = source[source.index('static int same_content('):
                   source.index('/* Bytes free on the mounted work partition')]
prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct { char name[120]; char data[80]; int len; } files[8];
static int corrupt, fail_write, fail_publish;
static int find(const char *p) {
  for (int i=0;i<8;i++) if (!strcmp(files[i].name,p)) return i;
  return -1;
}
int64_t file_size(const char *p) { int i=find(p); return i<0 ? -1 : files[i].len; }
int file_load(const char *p, void **out, uint32_t max) {
  int i=find(p); *out=NULL;
  if (i<0 || (uint32_t)files[i].len>=max) return -1;
  *out=malloc(files[i].len); memcpy(*out,files[i].data,files[i].len);
  if (corrupt && strstr(p,".tmp")) ((char *)*out)[0]^=1;
  return files[i].len;
}
int fileXioRemove(const char *p) { int i=find(p); if(i>=0) files[i].name[0]=0; return 0; }
int file_write_all(const char *p, const void *data, uint32_t len) {
  if(fail_write) return -5;
  int i=find(p); if(i<0) for(i=0;i<8 && files[i].name[0];i++);
  assert(i<8 && len<80); strcpy(files[i].name,p); memcpy(files[i].data,data,len); files[i].len=len;
  return 0;
}
int fileXioRename(const char *a, const char *b) {
  if (fail_publish && strstr(a,".tmp")) return -5;
  int i=find(a); if(i<0 || find(b)>=0) return -5;
  strcpy(files[i].name,b); return 0;
}
'''
tests = r'''
static void reset(void) { memset(files,0,sizeof(files)); corrupt=fail_write=fail_publish=0; assert(file_write_all("card","old saves",9)==0); }
static void unchanged(void) { int i=find("card"); assert(i>=0 && files[i].len==9 && !memcmp(files[i].data,"old saves",9)); }
int main(void) {
  reset(); corrupt=1; assert(extras_write_verified("card","new saves",9)<0); unchanged();
  reset(); fail_write=1; assert(extras_write_verified("card","new saves",9)<0); unchanged();
  reset(); fail_publish=1; assert(extras_write_verified("card","new saves",9)<0); unchanged();
  reset(); assert(extras_write_verified("card","new saves",9)==0); assert(same_content("card","new saves",9)); assert(find("card.old")<0);
  reset(); assert(fileXioRename("card","card.old")==0); fail_publish=1;
  assert(extras_write_verified("card","new saves",9)<0); unchanged();
  puts("Production extras verified-write and rollback tests passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'extras.c'
    src.write_text(prelude + functions + tests)
    binary = Path(tmp) / 'extras'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
