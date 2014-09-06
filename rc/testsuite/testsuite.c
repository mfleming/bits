/*
Copyright (c) 2013, Intel Corporation
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

#include <grub/extcmd.h>
#include <grub/types.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/dl.h>
#include <grub/term.h>
#include <grub/time.h>
#include "c.h"
#include "datatype.h"
#include "bitsutil.h"
#include "smp.h"

GRUB_MOD_LICENSE("GPLv3+");
GRUB_MOD_DUAL_LICENSE("3-clause BSD");

static U32 ncpus = 0;
static const CPU_INFO *cpu;
#define ALL_CPUS (~0U)

static grub_err_t parse_cpu_num(char *str, U32 * out)
{
    U32 num;

    grub_errno = GRUB_ERR_NONE;

    if (grub_strcmp(str, "all") == 0) {
        *out = ALL_CPUS;
        return GRUB_ERR_NONE;
    }

    if (strtou32_h(str, &num) != GRUB_ERR_NONE)
        return grub_errno;

    if (num >= ncpus)
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "CPU number %u invalid", num);

    *out = num;
    return GRUB_ERR_NONE;
}

static U32 viewpoint_env(void)
{
    U32 num;
    const char *env = grub_env_get("viewpoint");
    if (!env)
        return ALL_CPUS;
    if (strtou32_h(env, &num) != GRUB_ERR_NONE || num >= ncpus) {
        grub_printf("viewpoint environment variable contains invalid value \"%s\"\n", env);
        return ALL_CPUS;
    }
    return num;
}

static U32 first_cpu(U32 cpu_num)
{
    return cpu_num == ALL_CPUS ? 0 : cpu_num;
}

static U32 last_cpu(U32 cpu_num)
{
    return cpu_num == ALL_CPUS ? ncpus : cpu_num + 1;
}

static grub_err_t init(void)
{
    ncpus = smp_init();
    if (!ncpus)
        return grub_error(GRUB_ERR_IO, "Failed to initialize SMP");

    cpu = smp_read_cpu_list();
    if (!cpu)
        return grub_error(GRUB_ERR_IO, "Failed to initialize SMP (smp_read_cpu_list)");

    return GRUB_ERR_NONE;
}

static const char c_help[] =
    "Usage: c \"C-style expression with space-separated tokens\"\n"
    "Evaluate a C expression\n"
    "All evaluation occurs on 64-bit unsigned integers, specified as decimal\n"
    "numbers, hex numbers prefixed by 0x, or named variables taken from the\n"
    "environment.  Variable assignments work, and set variables in the environment.\n"
    "Returns true or false based on the final value of the expression, so\n"
    "\"if c ... \" and \"while c ...\" work.\n"
    "\n"
    "Supported operators, in order from highest to lowest precedence:\n"
    " ()        (parentheses)\n"
    " ! ~ ++ -- (both pre- and post- increment and decrement)\n"
    " * / %\n"
    " + -\n"
    " << >>\n"
    " < <= > >=\n"
    " == !=\n"
    " &         (bitwise and)\n"
    " ^         (bitwise xor)\n"
    " |         (bitwise or)\n"
    " &&        (logical and; WARNING: does not short-circuit)\n"
    " ||        (logical or;  WARNING: does not short-circuit)\n"
    " = += -= *= /= %= <<= >>= &= ^= |=\n"
    " ,         (comma operator)\n";

static grub_err_t grub_cmd_c(grub_command_t cmd, int argc, char **args)
{
    U64 result;
    (void)cmd;

    if (argc == 1 && grub_strcmp(args[0], "--help") == 0) {
        grub_printf("%s", c_help);
        return GRUB_ERR_NONE;
    }

    if (c_expr(argc, args, &result))
        return !result;
    else
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Failed to parse C expression");
}

static void noop_callback(void *param)
{
    (void)param;
}

static const struct grub_arg_option cpu_ping_options[] = {
#undef OPTION_CPU
#define OPTION_CPU 0
    {"cpu", 'c', 0, "CPU number", "CPU", ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0}
};

static grub_err_t grub_cmd_cpu_ping(struct grub_extcmd_context *context, int argc, char **args)
{
    U32 i, j;
    U32 cpuNum;
    U32 repeat_count;
    U64 start, stop;
    U32 seconds = 0;

    if (init() != GRUB_ERR_NONE)
        return grub_errno;

    cpuNum = viewpoint_env();

    if (context->state[OPTION_CPU].set)
        if (parse_cpu_num(context->state[OPTION_CPU].arg, &cpuNum) != GRUB_ERR_NONE)
            return grub_errno;

    if (argc != 1)
        return grub_error(GRUB_ERR_BAD_ARGUMENT, "Need 1 argument: repeat_count");
    if (strtou32_h(args[0], &repeat_count) != GRUB_ERR_NONE)
        return grub_errno;

    start = grub_get_time_ms();
    for (j = 0; j < repeat_count; j++) {
        if (grub_getkey_noblock() == GRUB_TERM_ESC)
            break;
        stop = grub_get_time_ms();
        if (stop - start > 1000) {
            start = stop;
            seconds++;
            grub_printf("\r%u second%s (%u%%)", seconds, seconds == 1 ? "" : "s", (j * 100) / repeat_count);
        }
        for (i = first_cpu(cpuNum); i != last_cpu(cpuNum); i++)
            smp_function(cpu[i].apicid, noop_callback, NULL);
    }
    grub_printf("\r");

    return GRUB_ERR_NONE;
}

static grub_command_t cmd_c;
static grub_extcmd_t cmd_cpu_ping;

GRUB_MOD_INIT(testsuite)
{
  cmd_c = grub_register_command("c", grub_cmd_c, "\"C-style expression with space-separated tokens\"", "Evaluate a C expression.");
  cmd_cpu_ping = grub_register_extcmd("cpu_ping", grub_cmd_cpu_ping, 0,
                                      "[-c cpu_num] count",
                                      "Ping CPU",
                                      cpu_ping_options);
}

GRUB_MOD_FINI(testsuite)
{
    grub_unregister_extcmd(cmd_cpu_ping);
    grub_unregister_command(cmd_c);
}
