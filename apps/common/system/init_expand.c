#include "app_config.h"
#include "asm/includes.h"
#include "system/init.h"
#include "system/task.h"
#include "app_core.h"
#include "irq.h"
#include "version.h"
#include "jiffies.h"

__attribute__((noreturn))
void __cxa_pure_virtual(void)
{
    printf("Need to make sure \"__cxa_pure_virtual\" runs OK!");
    while (1);
}

__attribute__((noreturn))
void __cxa_deleted_virtual(void)
{
    printf("Need to make sure \"__cxa_deleted_virtual\" runs OK!");
    while (1);
}

int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso)
{
    // printf("Need to make sure \"__cxa_atexit\" runs OK!");
    // 调用这个函数来注册全局变量的析构函数
    // 析构全局变量的时候，会调用这里注册的东西
    return 0;
}
void __cxa_finalize(void *f)
{
    printf("Need to make sure \"__cxa_finalize\" runs OK!");
}

void *__dso_handle __attribute__((__visibility__("hidden")));

int swprintf(wchar_t *ws, size_t len, const wchar_t *format, ...)
{
    printf("Need to make sure \"swprintf\" runs OK!");
    return -1;
}

char *fgets(char *str, int n, FILE *stream)
{
    printf("Need to make sure \"fgets\" runs OK!");
    return 0;
}

/* int feof(FILE *stream){ */
/* printf("Need to make sure \"feof\" runs OK!"); */
/* return 0; */
/* } */

int fflush(FILE *stream)
{
    printf("Need to make sure \"fflush\" runs OK!");
    return 0;
}

int fprintf(FILE *stream, const char *format, ...)
{
    printf("Need to make sure \"fprintf\" runs OK!");
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    return 0;
}

int fputs(const char *str, FILE *stream)
{
    printf("%s  %d not define \n", __func__, __LINE__);
}

int vasprintf(char **buf, const char *fmt, va_list ap)
{
    printf("Need to make sure \"vasprintf\" runs OK!");
    int chars;
    char *b;
    static char _T_emptybuffer = '\0';

    if (!buf) {
        return -1;
    }

    chars = vsnprintf(&_T_emptybuffer, 0, fmt, ap) + 1;
    if (chars < 0) {
        chars *= -1;    /* CAW: old glibc versions have this problem */
    }

    b = (char *)malloc(sizeof(char) * chars);
    if (!b) {
        return -1;
    }

    if ((chars = vsprintf(b, fmt, ap)) < 0) {
        free(b);
    } else {
        *buf = b;
    }

    return chars;
}

int atexit(void (*func)(void))
{
    printf("Need to make sure \"atexit\" runs OK!");
    return 0;
}

void *_malloc_r(size_t sz)
{
    return malloc(sz);
}

void _free_r(void *p)
{
    return free(p);
}

void *_calloc_r(struct _reent *ptr, size_t size, size_t len)
{
    void *pt = calloc(size, len);
    return pt;
}


//fix me
/* int fseeko(FILE *stream, int offset, int fromwhere) */
/* { */
/* return fseek(stream, offset, fromwhere); */
/* } */

/* int ftello(FILE *stream) */
/* { */
/* return ftell(stream); */
/* } */

/* void *fdopen(int fd, const char *mode) */
/* { */
/* printf("[%s, %d]is not supported yet\n", __FUNCTION__, __LINE__); */
/* return (void *)0; */
/* } */

