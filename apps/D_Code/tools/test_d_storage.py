"""Compile production d_storage.c against SDK-boundary stubs and exercise queries."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import argparse
import shutil

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work-dir', type=Path, default=ROOT / 'apps/D_Code/build/out/storage_tests')
parser.add_argument('--compiler', default='gcc')
options = parser.parse_args()
OUT = options.work_dir.resolve()
if not OUT.is_relative_to(ROOT) or OUT == ROOT:
    raise ValueError('work directory must stay inside this project')
OUT.mkdir(parents=True, exist_ok=True)
TEMP = OUT / "temp"
TEMP.mkdir(exist_ok=True)
environment = dict(os.environ, TEMP=str(TEMP), TMP=str(TEMP), TMPDIR=str(TEMP))
headers = {
    "config/app_config.h": r'''
#define TCFG_SD0_ENABLE 1
#define TCFG_SD1_ENABLE 1
#define TCFG_UDISK_ENABLE 1
#define CONFIG_MUSIC_PATH_SD0 "storage/sd0/C/"
#define CONFIG_MUSIC_PATH_SD1 "storage/sd1/C/"
#define CONFIG_MUSIC_PATH_UDISK0 "storage/udisk0/C/"
#define CONFIG_MUSIC_PATH_UDISK1 "storage/udisk1/C/"
#define CONFIG_MUSIC_PATH_FLASH "mnt/sdfile/res/"
#define CONFIG_REC_PATH_0 "storage/sd0/C/DCIM/1/"
#define CONFIG_UPGRADE_PATH "storage/sd0/C/update.ufw"
#define CONFIG_UDISK_UPGRADE_PATH "storage/udisk0/C/update.ufw"
''',
    "system/includes.h": r'''
#include <stddef.h>
#include <stdint.h>
int printf(const char *, ...);
int snprintf(char *, size_t, const char *, ...);
''',
    "fs/fs.h": r'''
#ifndef HOST_FS_H
#define HOST_FS_H
typedef struct host_file FILE;
struct vfs_attr { unsigned int fsize; };
/* This declaration matches the SDK FILE* contract. No stub implementation:
 * an accidental capacity query causes a link error instead of silent success. */
int fget_attrs(FILE *, struct vfs_attr *);
int fmount_exist(const char *);
#define fopen host_fopen
#define fclose host_fclose
#define opendir host_opendir
#define closedir host_closedir
FILE *host_fopen(const char *, const char *);
int host_fclose(FILE *);
void *host_opendir(const char *);
int host_closedir(void *);
int fdelete_by_name(const char *);
#endif
''',
    "device/device.h": "int dev_online(const char *);\n",
    "os/os_api.h": "/* No OS functions used by this production module. */\n",
    "sys/stat.h": "/* No mkdir call used by this production module. */\n",
}
for relative, text in headers.items():
    destination = OUT / "include" / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(text.lstrip(), encoding="utf-8")

harness = OUT / "storage_runtime_tests.c"
harness.write_text(r'''
#include "d_storage.h"
#include "fs/fs.h"
#include <assert.h>
#include <string.h>
static int online = 1;
static int io_calls = 0;
int dev_online(const char *name) { assert(name); return online; }
int fmount_exist(const char *path) { assert(path); return online; }
FILE *host_fopen(const char *path, const char *mode)
{ (void)path; (void)mode; ++io_calls; return 0; }
int host_fclose(FILE *file) { (void)file; ++io_calls; return 0; }
void *host_opendir(const char *path) { (void)path; ++io_calls; return 0; }
int host_closedir(void *dir) { (void)dir; ++io_calls; return 0; }
int fdelete_by_name(const char *path) { (void)path; ++io_calls; return 0; }

int main(void)
{
    d_storage_info_t info;
    unsigned char untouched[sizeof(info)];
    const int invalid[] = {-1, -100, D_STORAGE_MAX, 100};
    assert(d_storage_init() == 0);
    for (int type = D_STORAGE_SD0; type < D_STORAGE_MAX; ++type) {
        memset(&info, 0xa5, sizeof(info));
        assert(d_storage_get_info((d_storage_type_t)type, &info) == 0);
        assert(info.type == (d_storage_type_t)type);
        assert(info.status == D_STORAGE_MOUNTED);
        assert(info.dev_name && info.root_path);
        assert(info.total_size == 0 && info.free_size == 0);
        assert(d_storage_get_free_space((d_storage_type_t)type) == 0);
    }
    online = 0;
    memset(&info, 0xa5, sizeof(info));
    assert(d_storage_get_info(D_STORAGE_SD0, &info) == 0);
    assert(info.status == D_STORAGE_OFFLINE);
    assert(info.total_size == 0 && info.free_size == 0);
    for (unsigned int index = 0; index < sizeof(invalid)/sizeof(invalid[0]); ++index) {
        d_storage_type_t type = (d_storage_type_t)invalid[index];
        memset(&info, 0xa5, sizeof(info));
        memcpy(untouched, &info, sizeof(info));
        assert(d_storage_get_info(type, &info) < 0);
        assert(memcmp(untouched, &info, sizeof(info)) == 0);
        assert(d_storage_mount(type) < 0);
        assert(d_storage_unmount(type) < 0);
        assert(d_storage_get_free_space(type) == 0);
    }
    assert(d_storage_get_info(D_STORAGE_SD0, 0) < 0);
    assert(d_storage_unmount(D_STORAGE_FLASH) < 0);
    /* Querying unknown capacity must not perform any file I/O. */
    assert(io_calls == 0);
    return 0;
}
'''.lstrip(), encoding="utf-8")
source = ROOT / "apps/D_Code/platform/storage/d_storage.c"
executable = OUT / "storage_runtime_tests.exe"
compiler = shutil.which(options.compiler)
if compiler is None:
    raise RuntimeError("host C compiler not found: " + options.compiler)
command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-I" + str(OUT / "include"), "-I" + str(source.parent),
           str(source), str(harness), "-o", str(executable)]
compiled = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, text=True)
(OUT / "compiler.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
compiled.check_returncode()
run = subprocess.run([str(executable)], cwd=OUT, env=environment, capture_output=True, text=True, timeout=5)
(OUT / "runtime.log").write_text(run.stdout + run.stderr, encoding="utf-8")
run.check_returncode()
result = {"result": "PASS", "production_source": str(source),
          "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
          "cases": ["5 mounted storage kinds", "offline status", "unknown capacity overwrites caller data",
                    "negative and out-of-range get/mount/unmount/free-space", "NULL output",
                    "invalid get leaves output unchanged", "no query file I/O", "no fget_attrs link symbol"],
          "boundary": "Production storage code, host C runtime, SDK device/filesystem boundaries replaced"}
(OUT / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print("Storage production query: PASS (unknown capacities, invalid arguments, no file-attribute access)")
