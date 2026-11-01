/*
 * Copyright 2002 Jon Griffiths
 * Copyright 2016 Sebastian Lackner
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

// Parser adapted from Wine 10 dlls/shcore/main.c; ABI: shell32.spec.
#pragma once
#include <windows.h>
#include <cstring>
#include <cstdint>
#include <string>
#include <cwchar>
#include "file_operation.h"
#include "execute.h"
#include "folders.h"
#include "create_directory.h"
namespace shell32_core {
inline std::wstring image_path;
// Configure only before guest threads start. Empty clears process identity.
inline void configure_image_path(const WCHAR *path) { image_path = path ? path : L""; }
inline WCHAR** WINAPI command_line_to_argv(const WCHAR *cmdline, int *numargs)
{
    int qcount, bcount;
    const WCHAR *s;
    WCHAR **argv;
    DWORD argc;
    WCHAR *d;

    if (!numargs)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    if (*cmdline == 0)
    {
        // The loader may configure a guest image path. Never query the host
        // executable: it belongs to the launcher, not the Windows process.
        if (image_path.empty())
        {
            SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return NULL;
        }
        size_t bytes = (image_path.size() + 1) * sizeof(WCHAR);
        argv = static_cast<WCHAR **>(LocalAlloc(LMEM_FIXED, 2 * sizeof(WCHAR *) + bytes));
        if (!argv) return NULL;
        argv[0] = reinterpret_cast<WCHAR *>(argv + 2);
        memcpy(argv[0], image_path.c_str(), bytes);
        argv[1] = NULL;
        *numargs = 1;
        return argv;
    }

    /* --- First count the arguments */
    argc = 1;
    s = cmdline;
    /* The first argument, the executable path, follows special rules */
    if (*s == '"')
    {
        /* The executable path ends at the next quote, no matter what */
        s++;
        while (*s)
            if (*s++ == '"')
                break;
    }
    else
    {
        /* The executable path ends at the next space, no matter what */
        while (*s && *s != ' ' && *s != '\t')
            s++;
    }
    /* skip to the first argument, if any */
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s)
        argc++;

    /* Analyze the remaining arguments */
    qcount = bcount = 0;
    while (*s)
    {
        if ((*s == ' ' || *s == '\t') && qcount == 0)
        {
            /* skip to the next argument and count it if any */
            while (*s == ' ' || *s == '\t')
                s++;
            if (*s)
                argc++;
            bcount = 0;
        }
        else if (*s == '\\')
        {
            /* '\', count them */
            bcount++;
            s++;
        }
        else if (*s == '"')
        {
            /* '"' */
            if ((bcount & 1) == 0)
                qcount++; /* unescaped '"' */
            s++;
            bcount = 0;
            /* consecutive quotes, see comment in copying code below */
            while (*s == '"')
            {
                qcount++;
                s++;
            }
            qcount = qcount % 3;
            if (qcount == 2)
                qcount = 0;
        }
        else
        {
            /* a regular character */
            bcount = 0;
            s++;
        }
    }

    /* Allocate in a single lump, the string array, and the strings that go
     * with it. This way the caller can make a single LocalFree() call to free
     * both, as per MSDN.
     */
    argv = static_cast<WCHAR **>(LocalAlloc(LMEM_FIXED, (argc + 1) * sizeof(WCHAR *) + (wcslen(cmdline) + 1) * sizeof(WCHAR)));
    if (!argv)
        return NULL;

    /* --- Then split and copy the arguments */
    argv[0] = d = wcscpy((WCHAR *)(argv + argc + 1), cmdline);
    argc = 1;
    /* The first argument, the executable path, follows special rules */
    if (*d == '"')
    {
        /* The executable path ends at the next quote, no matter what */
        s = d + 1;
        while (*s)
        {
            if (*s == '"')
            {
                s++;
                break;
            }
            *d++ = *s++;
        }
    }
    else
    {
        /* The executable path ends at the next space, no matter what */
        while (*d && *d != ' ' && *d != '\t')
            d++;
        s = d;
        if (*s)
            s++;
    }
    /* close the executable path */
    *d++ = 0;
    /* skip to the first argument and initialize it if any */
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s)
    {
        /* There are no parameters so we are all done */
        argv[argc] = NULL;
        *numargs = argc;
        return argv;
    }

    /* Split and copy the remaining arguments */
    argv[argc++] = d;
    qcount = bcount = 0;
    while (*s)
    {
        if ((*s == ' ' || *s == '\t') && qcount == 0)
        {
            /* close the argument */
            *d++ = 0;
            bcount = 0;

            /* skip to the next one and initialize it if any */
            do {
                s++;
            } while (*s == ' ' || *s == '\t');
            if (*s)
                argv[argc++] = d;
        }
        else if (*s=='\\')
        {
            *d++ = *s++;
            bcount++;
        }
        else if (*s == '"')
        {
            if ((bcount & 1) == 0)
            {
                /* Preceded by an even number of '\', this is half that
                 * number of '\', plus a quote which we erase.
                 */
                d -= bcount / 2;
                qcount++;
            }
            else
            {
                /* Preceded by an odd number of '\', this is half that
                 * number of '\' followed by a '"'
                 */
                d = d - bcount / 2 - 1;
                *d++ = '"';
            }
            s++;
            bcount = 0;
            /* Now count the number of consecutive quotes. Note that qcount
             * already takes into account the opening quote if any, as well as
             * the quote that lead us here.
             */
            while (*s == '"')
            {
                if (++qcount == 3)
                {
                    *d++ = '"';
                    qcount = 0;
                }
                s++;
            }
            if (qcount == 2)
                qcount = 0;
        }
        else
        {
            /* a regular character */
            *d++ = *s++;
            bcount = 0;
        }
    }
    *d = '\0';
    argv[argc] = NULL;
    *numargs = argc;

    return argv;
}

inline uintptr_t resolve(const char *dll, const char *name) {
    if (!dll || !name || _stricmp(dll, "shell32.dll")) return 0;
    if (!strcmp(name, "SHCreateDirectoryExW")) return reinterpret_cast<uintptr_t>(&create_directory);
    if (!strcmp(name, "SHGetFolderPathW")) return reinterpret_cast<uintptr_t>(&folder_path);
    if (!strcmp(name, "SHGetKnownFolderPath")) return reinterpret_cast<uintptr_t>(&known_folder_path);
    if (!strcmp(name, "ShellExecuteW")) return reinterpret_cast<uintptr_t>(&execute);
    if (!strcmp(name, "SHFileOperationW")) return reinterpret_cast<uintptr_t>(&file_operation);
    if (!strcmp(name, "CommandLineToArgvW")) return reinterpret_cast<uintptr_t>(&command_line_to_argv);
    return 0;
}
}
