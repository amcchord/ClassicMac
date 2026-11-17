/*
 * The RetroBoot OS X renderer uses Linux EGL. The Windows OS X runtime
 * uses standard VGA; provide the optional Radeon backend's failure path.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <string.h>
#include "ati_gl.h"

bool ati_gl_open(ATIGL *gl, char *info, size_t infolen)
{
    memset(gl, 0, sizeof(*gl));
    snprintf(info, infolen, "RetroBoot EGL is unavailable on Windows");
    return false;
}

void ati_gl_close(void)
{
}
