/*
 * Copyright 2018 Nikolay Sivov
 * Copyright 2018 Zhiyi Zhang
 * Copyright 2021-2023 Zebediah Figura
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

// Adapted from Wine 10 dlls/kernelbase/path.c. Pure lexical operations; no host paths.
#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
#include "registry.h"
namespace shlwapi_core {
inline BOOL WINAPI is_unc_server_share(const WCHAR *path)
{
    BOOL seen_slash = FALSE;


    if (path && *path++ == '\\' && *path++ == '\\')
    {
        while (*path)
        {
            if (*path == '\\')
            {
                if (seen_slash)
                    return FALSE;
                seen_slash = TRUE;
            }

            path++;
        }
    }

    return seen_slash;
}

inline BOOL WINAPI canonicalize(WCHAR *buffer, const WCHAR *path)
{
    const WCHAR *src = path;
    WCHAR *dst = buffer;


    if (dst)
        *dst = '\0';

    if (!dst || !path)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!*path)
    {
        *buffer++ = '\\';
        *buffer = '\0';
        return TRUE;
    }

    /* Copy path root */
    if (*src == '\\')
    {
        *dst++ = *src++;
    }
    else if (*src && src[1] == ':')
    {
        /* X:\ */
        *dst++ = *src++;
        *dst++ = *src++;
        if (*src == '\\')
            *dst++ = *src++;
    }

    /* Canonicalize the rest of the path */
    while (*src)
    {
        if (*src == '.')
        {
            if (src[1] == '\\' && (src == path || src[-1] == '\\' || src[-1] == ':'))
            {
                src += 2; /* Skip .\ */
            }
            else if (src[1] == '.' && dst != buffer && dst[-1] == '\\')
            {
                /* \.. backs up a directory, over the root if it has no \ following X:.
                 * .. is ignored if it would remove a UNC server name or initial \\
                 */
                if (dst != buffer)
                {
                    *dst = '\0'; /* Allow PathIsUNCServerShareA test on lpszBuf */
                    if (dst > buffer + 1 && dst[-1] == '\\' && (dst[-2] != '\\' || dst > buffer + 2))
                    {
                        if (dst[-2] == ':' && (dst > buffer + 3 || dst[-3] == ':'))
                        {
                            dst -= 2;
                            while (dst > buffer && *dst != '\\')
                                dst--;
                            if (*dst == '\\')
                                dst++; /* Reset to last '\' */
                            else
                                dst = buffer; /* Start path again from new root */
                        }
                        else if (dst[-2] != ':' && !is_unc_server_share(buffer))
                            dst -= 2;
                    }
                    while (dst > buffer && *dst != '\\')
                        dst--;
                    if (dst == buffer)
                    {
                        *dst++ = '\\';
                        src++;
                    }
                }
                src += 2; /* Skip .. in src path */
            }
            else
                *dst++ = *src++;
        }
        else
            *dst++ = *src++;
    }

    /* Append \ to naked drive specs */
    if (dst - buffer == 2 && dst[-1] == ':')
        *dst++ = '\\';
    *dst++ = '\0';
    return TRUE;
}

inline uintptr_t resolve(const char* dll, const char* name) {
    if (!dll || !name || _stricmp(dll, "shlwapi.dll")) return 0;
    if (!strcmp(name, "SHDeleteKeyW")) return reinterpret_cast<uintptr_t>(&delete_key);
    if (!strcmp(name, "PathCanonicalizeW")) return reinterpret_cast<uintptr_t>(&canonicalize);
    return 0;
}
}
