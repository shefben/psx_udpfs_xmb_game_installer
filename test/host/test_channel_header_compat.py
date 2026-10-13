"""Exercise production discovery against new, old and invalid channel headers."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/pfs_channel.c').read_text()
start=source.index('inst_err_t channel_quick_check(')
end=source.index('\n#define INFO_SYS_MAX',start)
function=source[start:end]
prelude=r'''
#include <assert.h>
#include <string.h>
#include "errors.h"
#include "xmb_text.h"
#define W "pfs1:"
#define FIO_MT_RDONLY 1
static int kind, calls;
int pfs_mount(const char *w,const char *p,int mode) { (void)w;(void)p;(void)mode;return 0; }
void pfs_umount(const char *w) { (void)w; }
int file_size(const char *p) { (void)p;return 1; }
inst_err_t ppaa_check_partition(const char *p,const char *s,size_t n,int k,int *r) {
 (void)p;(void)k;(void)r;calls++;assert(n==strlen(s));
 if(kind==1 && !strcmp(s,XMB_SYSTEM_CNF))return ERR_OK;
 if(kind==2 && !strcmp(s,"BOOT2 = pfs:/EXECUTE.KELF\nVER = 1.00\nVMODE = NTSC\nHDDUNITPOWER = NICHDD\n"))return ERR_OK;
 return ERR_XMB_VERIFY;
}
'''
main=r'''
int main(void) {
 kind=1;calls=0;assert(channel_quick_check("PP.GAME")==ERR_OK && calls==1);
 kind=2;calls=0;assert(channel_quick_check("PP.GAME")==ERR_OK && calls==2);
 kind=3;calls=0;assert(channel_quick_check("PP.GAME")==ERR_XMB_VERIFY && calls==2);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 td=Path(td);c=td/'check.c';c.write_text(prelude+function+main)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(c),'-o',str(td/'check')],check=True)
 subprocess.run([str(td/'check')],check=True)
print('New and legacy channel headers recognized; invalid header rejected')
