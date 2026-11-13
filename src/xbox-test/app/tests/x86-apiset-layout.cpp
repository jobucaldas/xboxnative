// SPDX-License-Identifier: GPL-3.0-or-later
// Print PE32 CRT structure sizes and offsets from the real mingw-w64 headers.
#include <stddef.h>
#include <stdio.h>
#include <locale.h>
#include <time.h>
#include <sys/stat.h>
#include <io.h>
#include <sys/timeb.h>
#include <stdlib.h>
#include <math.h>

#define SIZE(label, T) (unsigned)sizeof(T)
#define FIELD(label, T, member) (unsigned)offsetof(T, member)
#define TIMES(label, T) SIZE(label, T), FIELD(label, T, tm_sec), FIELD(label, T, tm_isdst)
#define STAT(label, T) SIZE(label, T), FIELD(label, T, st_ino), FIELD(label, T, st_size), FIELD(label, T, st_atime), FIELD(label, T, st_mtime), FIELD(label, T, st_ctime)
#define FIND(label, T) SIZE(label, T), FIELD(label, T, time_create), FIELD(label, T, size), FIELD(label, T, name)
#define TIMEB(label, T) SIZE(label, T), FIELD(label, T, time), FIELD(label, T, millitm), FIELD(label, T, timezone), FIELD(label, T, dstflag)
#define DIV(label, T) SIZE(label, T), FIELD(label, T, quot), FIELD(label, T, rem)

const unsigned int layout_data[] __attribute__((section(".rdata"))) = {
    0x58424c59,
    SIZE("lconv", struct lconv),
    FIELD("lconv", struct lconv, decimal_point),
    FIELD("lconv", struct lconv, int_frac_digits),
#if defined(_UCRT)
    FIELD("lconv", struct lconv, _W_decimal_point),
    FIELD("lconv", struct lconv, _W_negative_sign),
#else
    0xffffffff, 0xffffffff,
#endif
    TIMES("tm", struct tm),
    STAT("_stat64", struct _stat64), STAT("_stat64i32", struct _stat64i32),
    STAT("_stat32", struct _stat32), STAT("stat", struct stat),
    FIND("_finddata_t", struct _finddata_t), FIND("_finddatai64_t", struct _finddatai64_t),
    FIND("_finddata32_t", struct _finddata32_t), FIND("_finddata32i64_t", struct _finddata32i64_t),
    FIND("_finddata64i32_t", struct _finddata64i32_t), FIND("__finddata64_t", struct __finddata64_t),
    FIND("_wfinddata_t", struct _wfinddata_t), FIND("_wfinddatai64_t", struct _wfinddatai64_t),
    FIND("_wfinddata32_t", struct _wfinddata32_t), FIND("_wfinddata32i64_t", struct _wfinddata32i64_t),
    FIND("_wfinddata64i32_t", struct _wfinddata64i32_t), FIND("_wfinddata64_t", struct _wfinddata64_t),
    TIMEB("_timeb", struct _timeb), TIMEB("__timeb64", struct __timeb64),
    DIV("div_t", div_t), DIV("ldiv_t", ldiv_t), DIV("lldiv_t", lldiv_t),
    SIZE("_exception", struct _exception),
    FIELD("_exception", struct _exception, type),
    FIELD("_exception", struct _exception, name),
    FIELD("_exception", struct _exception, arg1),
    FIELD("_exception", struct _exception, retval),
    SIZE("_complex", struct _complex),
    FIELD("_complex", struct _complex, x),
    FIELD("_complex", struct _complex, y),
    SIZE("FILE", FILE),
#ifdef _UCRT
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    FIELD("FILE", struct _iobuf, _Placeholder),
#else
    FIELD("FILE", struct _iobuf, _ptr),
    FIELD("FILE", struct _iobuf, _cnt),
    FIELD("FILE", struct _iobuf, _base),
    FIELD("FILE", struct _iobuf, _flag),
    FIELD("FILE", struct _iobuf, _file),
    FIELD("FILE", struct _iobuf, _charbuf),
    FIELD("FILE", struct _iobuf, _bufsiz),
    FIELD("FILE", struct _iobuf, _tmpfname),
    0xffffffff,
#endif
};
