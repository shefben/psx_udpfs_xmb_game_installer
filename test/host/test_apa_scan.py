#!/usr/bin/env python3
"""Production APA scan: large libraries, DMA alignment, cycles and errors."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/hdd_partitions.c').read_text()
helpers = source[source.index('#define APA_SCAN_MAX'):source.index('/* Same argument')]
listing = source[source.index('int hdd_list('):source.index('int hdd_exists(')]
prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hdd_partitions.h"
typedef struct { struct { uint32_t private_5, mode, attr, size; } stat; char name[256]; } iox_dirent_t;
static int pos, closed, total, cycle_at, fail_at, waits;
void network_wait_idle(void) { waits++; }
int fileXioDopen(const char *p) { assert(!strcmp(p,"hdd0:"));pos=0;closed=0;return 1; }
int fileXioDclose(int dd) { assert(dd==1);closed++;return 0; }
int fileXioDread(int dd, iox_dirent_t *de) {
 assert(dd==1 && ((uintptr_t)de%64)==0);
 if(pos==fail_at)return -7;
 if(pos>=total)return 0;
 int n=pos++;
 de->stat.private_5=(n==cycle_at?0:(uint32_t)n)*0x40000;
 de->stat.mode=0x1337;de->stat.attr=n%24?1:0;de->stat.size=0x40000;
 snprintf(de->name,sizeof(de->name),"game-%d",n/24);return 10;
}
'''
tests = r'''
int main(void) {
 hdd_part_t out[64];
 total=32*24;cycle_at=fail_at=-1;
 assert(hdd_list(out,64)==32 && pos==total && closed==1 && waits==1);
 assert(hdd_list(out,1)==-75 && closed==1);
 cycle_at=600;assert(hdd_list(out,64)==-40 && closed==1);
 cycle_at=-1;fail_at=500;assert(hdd_list(out,64)==-7 && closed==1);
 fail_at=-1;total=0;assert(hdd_list(out,64)==0 && closed==1);
 puts("Production APA scan large-library, DMA alignment, cycle and error tests passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'scan.c'
    src.write_text(prelude + helpers + listing + tests)
    binary = Path(tmp) / 'scan'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(ROOT / 'src'), str(src),
                    str(ROOT / 'src/partname.c'), str(ROOT / 'src/util.c'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
