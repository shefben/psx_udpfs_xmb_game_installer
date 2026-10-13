"""Run the production PNG validator against Manager-encoded and damaged files."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as td:
    td=Path(td)
    harness=td/'check.c'
    harness.write_text('''#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "hdl_header.h"
int main(int argc,char **argv) {
 unsigned char b[65536]; FILE *f=fopen(argv[1],"rb"); assert(f);
 size_t n=fread(b,1,sizeof(b),f); fclose(f);
 assert(png_is_size(b,n,74,108));
 b[24]=16; assert(!png_is_size(b,n,74,108)); b[24]=8;
 b[25]=6; assert(!png_is_size(b,n,74,108)); b[25]=3;
 b[28]=1; assert(!png_is_size(b,n,74,108)); b[28]=0;
 b[n-1]^=1; assert(!png_is_size(b,n,74,108)); b[n-1]^=1;
 assert(!png_is_size(b,n-1,74,108));
 return 0;
}''')
    exe=td/'check'
    subprocess.run(['cc','-ffunction-sections','-Wl,--gc-sections','-I'+str(ROOT/'src'),str(harness),str(ROOT/'src/hdl_header.c'),str(ROOT/'src/crc32.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(ROOT/'test/fixtures/xmb_manager_jacket.png')],check=True)
print('XMB PNG format and corruption checks passed')
