/*
 * Filesystem abstraction over the ROM + data dir.
 *
 * A small POSIX-ish file API so the port layer has a uniform interface across
 * Windows/Linux/macOS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
extern void *calloc(unsigned long long count, unsigned long long size);
extern void free(void *ptr);
#include "platform.h"
#include "fs.h"
struct FSFile { FILE *f; };
FSFile *fsOpen(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
    FSFile *f = (FSFile *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->f = fopen(path, mode);
    if (!f->f) { free(f); return NULL; }
    return f;
}
void fsClose(FSFile *f) { if (f) { if (f->f) fclose(f->f); free(f); } }
int32_t fsRead(FSFile *f, void *buf, int32_t size)
{
    if (!f || !f->f || size < 0 || (!buf && size != 0)) return -1;
    size_t n = fread(buf, 1, (size_t)size, f->f);
    return n > INT32_MAX ? -1 : (int32_t)n;
}
int32_t fsWrite(FSFile *f, const void *buf, int32_t size)
{
    if (!f || !f->f || size < 0 || (!buf && size != 0)) return -1;
    size_t n = fwrite(buf, 1, (size_t)size, f->f);
    return n > INT32_MAX ? -1 : (int32_t)n;
}
int fsSeek(FSFile *f, int32_t offset, int whence)
{
    if (!f || !f->f) return -1;
    return fseek(f->f, (long)offset, whence);
}
int32_t fsTell(FSFile *f)
{
    if (!f || !f->f) return -1;
    long pos = ftell(f->f);
    return (pos < 0 || pos > INT32_MAX) ? -1 : (int32_t)pos;
}
int32_t fsSize(FSFile *f)
{
    if (!f || !f->f) return -1;
    long cur = ftell(f->f);
    if (cur < 0 || fseek(f->f, 0, SEEK_END) != 0) return -1;
    long sz = ftell(f->f);
    if (fseek(f->f, cur, SEEK_SET) != 0) return -1;
    return (sz < 0 || sz > INT32_MAX) ? -1 : (int32_t)sz;
}
int fsExists(const char *path)
{
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}
