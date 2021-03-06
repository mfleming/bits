/*
Copyright (c) 2010, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef BITSUTIL_H
#define BITSUTIL_H

#include <grub/err.h>
#include "datatype.h"

/* Transform a string to a U32.  Handles decimal, 0x-prefixed hex (C syntax),
 * or 'h'-suffixed hex (asm syntax).  Returns the parsed value in *value_ret.
 * If an error occurs, leaves *value_ret unchanged. */
grub_err_t strtou32_h(const char *str, U32 * value_ret);

/* As above, but for U64. */
grub_err_t strtou64_h(const char *str, U64 * value_ret);

/* Print information for debugging; only prints if the specified context
 * appears in the "debug" environment variable. */
void dprintf(const char *debug_context, const char *fmt, ...);

/* Print information for debugging; only prints if the specified context
 * appears in the "debug" environment variable. */
void dvprintf(const char *debug_context, const char *fmt, va_list args);

U64 mulU64byU64(U64 a, U64 b, U64 *high);
U64 divU64byU64(U64 n, U64 d, U64 *rem);

#endif /* BITSUTIL_H */
