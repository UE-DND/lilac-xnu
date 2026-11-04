#!/usr/bin/env python3
"""Exercise the desktop observer's real process sampling and log-tail code."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'userland/desktop-watch.c').read_text()
harness = r'''
#ifdef __linux__
struct host_statfs { char f_fstypename[16]; };
static int host_statfs(const char *path, struct host_statfs *status) {
    (void)path; (void)status; return -1;
}
#define statfs host_statfs
#endif
#define main desktop_observer_main
#define proc_listpids fake_list
#define proc_name fake_name
''' + source + r'''
#undef main
#include <assert.h>
static int scenario;
int fake_list(uint32_t type, uint32_t info, void *out, int size) {
    (void)type; (void)info;
    if(scenario==1)return -1;
    if(scenario==2)return size;
    int *p=out;p[0]=11;p[1]=12;p[2]=13;return 3*sizeof(*p);
}
int fake_name(int pid,void *out,uint32_t size) {
    const char *s=pid==11?"runningboardd":pid==12?"backboardd":"SpringBoard";
    snprintf(out,size,"%s",s);return (int)strlen(s);
}
int main(int argc,char **argv) {
    (void)argc;char line[64];
    assert(sample() && jobs[0].pid==11 && jobs[1].pid==12 && jobs[2].pid==13);
    scenario=1;assert(!sample() && !jobs[0].pid);
    scenario=2;assert(!sample());
    last_line(argv[1],line,sizeof(line));assert(!strcmp(line,"last line"));
    last_line("/definitely/not/a/lilac/log",line,sizeof(line));assert(strstr(line,"no log"));
    puts("PASS: desktop process identity, list errors/full buffer and log tail");
}
'''
with tempfile.TemporaryDirectory(prefix='lilac-desktop-') as temp:
    temp = Path(temp)
    (temp / 'test.c').write_text(harness)
    (temp / 'log').write_text('older\nlast line\n\n')
    subprocess.run(['clang', '-std=c11', '-D_DARWIN_C_SOURCE', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(temp / 'test.c'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test'), str(temp / 'log')], check=True)
