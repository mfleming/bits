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

#include <grub/env.h>
#include <grub/err.h>
#include <grub/misc.h>

#include "c.h"
#include "bitsutil.h"
#include "datatype.h"

#define snprintf grub_snprintf
#define strchr grub_strchr
#define strcmp grub_strcmp

static bool parse_num(const char *str, U64 * result)
{
    bool ret = strtou64_h(str, result);
    grub_errno = GRUB_ERR_NONE;
    return !ret;
}

struct parser_state {
    int argc;
    char **argv;
    char *arg;
    /* Previous state, for untoken */
    int prev_argc;
    char **prev_argv;
    char *prev_arg;
    char *eaten_space;
};

enum value_type {
    VALUE_FAIL,
    VALUE_VAL,
    VALUE_VAR,
};

struct value {
    enum value_type type;
    union {
        U64 val;
        const char *var;
    };
};

static struct value parse_expr(struct parser_state *state);

static const struct value fail = {
    .type = VALUE_FAIL,
};

static struct value eval_var(struct value value)
{
    if (value.type == VALUE_VAR) {
        const char *str = grub_env_get(value.var);
        dprintf("c", "eval_var with value.var=%s , grub_env_get returned %p %s\n", value.var, str, str);
        /* Allow referencing variables that don't exist, and treat them as 0 */
        if (!str) {
            value.type = VALUE_VAL;
            value.val = 0;
            return value;
        }
        value.type = parse_num(str, &value.val) ? VALUE_VAL : VALUE_FAIL;
    }
    return value;
}

static U64 eval_mod(U64 lhs, U64 rhs)
{
    U64 result;
    divU64byU64(lhs, rhs, &result);
    return result;
}

static U64 eval_div(U64 lhs, U64 rhs)
{
    return divU64byU64(lhs, rhs, NULL);
}

static U64 eval_mul(U64 lhs, U64 rhs)
{
    return lhs * rhs;
}

static U64 eval_sub(U64 lhs, U64 rhs)
{
    return lhs - rhs;
}

static U64 eval_add(U64 lhs, U64 rhs)
{
    return lhs + rhs;
}

static U64 eval_lshift(U64 lhs, U64 rhs)
{
    return lhs << rhs;
}

static U64 eval_rshift(U64 lhs, U64 rhs)
{
    return lhs >> rhs;
}

static U64 eval_less(U64 lhs, U64 rhs)
{
    return lhs < rhs;
}

static U64 eval_less_equal(U64 lhs, U64 rhs)
{
    return lhs <= rhs;
}

static U64 eval_greater(U64 lhs, U64 rhs)
{
    return lhs > rhs;
}

static U64 eval_greater_equal(U64 lhs, U64 rhs)
{
    return lhs >= rhs;
}

static U64 eval_equal(U64 lhs, U64 rhs)
{
    return lhs == rhs;
}

static U64 eval_not_equal(U64 lhs, U64 rhs)
{
    return lhs != rhs;
}

static U64 eval_bitand(U64 lhs, U64 rhs)
{
    return lhs & rhs;
}

static U64 eval_bitxor(U64 lhs, U64 rhs)
{
    return lhs ^ rhs;
}

static U64 eval_bitor(U64 lhs, U64 rhs)
{
    return lhs | rhs;
}

static U64 eval_and(U64 lhs, U64 rhs)
{
    return lhs && rhs;
}

static U64 eval_or(U64 lhs, U64 rhs)
{
    return lhs || rhs;
}

static U64 eval_comma(U64 lhs, U64 rhs)
{
    (void)lhs;
    return rhs;
}

static struct value eval_binop(struct value lhs, struct value rhs, void *extra)
{
    U64(*eval_op) (U64 lhs, U64 rhs) = extra;
    lhs = eval_var(lhs);
    rhs = eval_var(rhs);
    if (lhs.type != VALUE_VAL || rhs.type != VALUE_VAL)
        return fail;
    lhs.val = eval_op(lhs.val, rhs.val);
    return lhs;
}

static struct value eval_binop_nozerorhs(struct value lhs, struct value rhs, void *extra)
{
    U64(*eval_op) (U64 lhs, U64 rhs) = extra;
    lhs = eval_var(lhs);
    rhs = eval_var(rhs);
    if (lhs.type != VALUE_VAL || rhs.type != VALUE_VAL || rhs.val == 0)
        return fail;
    lhs.val = eval_op(lhs.val, rhs.val);
    return lhs;
}

static struct value eval_assign(struct value lhs, struct value rhs, void *extra)
{
    U64(*eval_op) (U64 lhs, U64 rhs) = extra;
    char valstr[sizeof("18446744073709551615")];
    struct value lhsval;
    if (lhs.type != VALUE_VAR)
        return fail;
    rhs = eval_var(rhs);
    if (rhs.type != VALUE_VAL)
        return fail;
    if (eval_op) {
        lhsval = eval_var(lhs);
        if (lhsval.type != VALUE_VAL)
            return fail;
        rhs.val = eval_op(lhsval.val, rhs.val);
    }
    snprintf(valstr, sizeof(valstr), "0x%llx", (unsigned long long)rhs.val);
    grub_env_set(lhs.var, valstr);
    return rhs;
}

static struct value eval_assign_nozerorhs(struct value lhs, struct value rhs, void *extra)
{
    U64(*eval_op) (U64 lhs, U64 rhs) = extra;
    char valstr[sizeof("18446744073709551615")];
    struct value lhsval;
    if (lhs.type != VALUE_VAR)
        return fail;
    rhs = eval_var(rhs);
    if (rhs.type != VALUE_VAL || rhs.val == 0)
        return fail;
    lhsval = eval_var(lhs);
    if (lhsval.type != VALUE_VAL)
        return fail;
    rhs.val = eval_op(lhsval.val, rhs.val);
    snprintf(valstr, sizeof(valstr), "0x%llx", (unsigned long long)rhs.val);
    grub_env_set(lhs.var, valstr);
    return rhs;
}

enum assoc {
    ASSOC_LEFT = 0,
    ASSOC_RIGHT = 1,
};

struct op {
    const char str[4];
    int precedence;
    enum assoc assoc;
    struct value (*eval) (struct value lhs, struct value rhs, void *extra);
    void *extra;
};

static const struct op op_table[] = {
    { .str = "%", .precedence = 12, .assoc = ASSOC_LEFT, .eval = eval_binop_nozerorhs, .extra = eval_mod },
    { .str = "/", .precedence = 12, .assoc = ASSOC_LEFT, .eval = eval_binop_nozerorhs, .extra = eval_div },
    { .str = "*", .precedence = 12, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_mul },
    { .str = "-", .precedence = 11, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_sub },
    { .str = "+", .precedence = 11, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_add },
    { .str = "<<", .precedence = 10, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_lshift },
    { .str = ">>", .precedence = 10, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_rshift },
    { .str = "<", .precedence = 9, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_less },
    { .str = "<=", .precedence = 9, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_less_equal },
    { .str = ">", .precedence = 9, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_greater },
    { .str = ">=", .precedence = 9, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_greater_equal },
    { .str = "==", .precedence = 8, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_equal },
    { .str = "!=", .precedence = 8, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_not_equal },
    { .str = "&", .precedence = 7, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_bitand },
    { .str = "^", .precedence = 6, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_bitxor },
    { .str = "|", .precedence = 5, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_bitor },
    { .str = "&&", .precedence = 4, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_and },
    { .str = "||", .precedence = 3, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_or },
    { .str = "%=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign_nozerorhs, .extra = eval_mod },
    { .str = "/=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign_nozerorhs, .extra = eval_div },
    { .str = "*=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_mul },
    { .str = "-=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_sub },
    { .str = "+=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_add },
    { .str = "<<=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_lshift },
    { .str = ">>=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_rshift },
    { .str = "&=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_bitand },
    { .str = "^=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_bitxor },
    { .str = "|=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = eval_bitor },
    { .str = "=", .precedence = 2, .assoc = ASSOC_RIGHT, .eval = eval_assign, .extra = NULL },
    { .str = ",", .precedence = 1, .assoc = ASSOC_LEFT, .eval = eval_binop, .extra = eval_comma },
};

static const char *get_token(struct parser_state *state)
{
    char *c;
    char *ret;
    state->eaten_space = NULL;
    state->prev_argc = state->argc;
    state->prev_argv = state->argv;
    state->prev_arg = state->arg;

    while (state->argc && (*state->arg == ' ' || !*state->arg)) {
        while (*state->arg == ' ')
            state->arg++;
        if (!*state->arg) {
            state->argc--;
            state->arg = *++state->argv;
        }
    }

    if (!state->argc)
        return NULL;

    ret = state->arg;
    c = strchr(state->arg, ' ');
    if (!c) {
        state->argc--;
        state->arg = *++state->argv;
        return ret;
    }
    state->eaten_space = c;
    *c = '\0';
    state->arg = c + 1;
    return ret;
}

static void untoken(struct parser_state *state)
{
    state->argc = state->prev_argc;
    state->argv = state->prev_argv;
    state->arg = state->prev_arg;
    if (state->eaten_space)
        *state->eaten_space = ' ';
}

static struct value parse_primary(struct parser_state *state)
{
    const struct value one = { .type = VALUE_VAL, { .val = 1 } };
    const char *token;
    struct value value;

    token = get_token(state);
    if (!token)
        return fail;
    if (strcmp(token, "(") == 0) {
        value = parse_expr(state);
        token = get_token(state);
        if (!token || strcmp(token, ")") != 0)
            value = fail;
    } else if (strcmp(token, "!") == 0) {
        value = eval_var(parse_primary(state));
        value.val = !value.val;
    } else if (strcmp(token, "~") == 0) {
        value = eval_var(parse_primary(state));
        value.val = ~value.val;
    } else if (strcmp(token, "++") == 0) {
        value = parse_primary(state);
        value = eval_assign(value, one, eval_add);
    } else if (strcmp(token, "--") == 0) {
        value = parse_primary(state);
        value = eval_assign(value, one, eval_sub);
    } else if (parse_num(token, &value.val))
        value.type = VALUE_VAL;
    else {
        value.type = VALUE_VAR;
        value.var = token;
        token = get_token(state);
        if (token) {
            if (strcmp(token, "++") == 0) {
                struct value temp = eval_var(value);
                eval_assign(value, one, eval_add);
                value = temp;
            } else if (strcmp(token, "--") == 0) {
                struct value temp = eval_var(value);
                eval_assign(value, one, eval_sub);
                value = temp;
            } else
                untoken(state);
        }
    }
    return value;
}

static const struct op *parse_op(struct parser_state *state)
{
    unsigned i;
    const char *token = get_token(state);
    if (!token)
        return NULL;
    for (i = 0; i < sizeof(op_table) / sizeof(*op_table); i++)
        if (strcmp(op_table[i].str, token) == 0)
            return &op_table[i];
    untoken(state);
    return NULL;
}

static struct value parse_expr_rhs(struct parser_state *state, struct value lhs, int min_precedence)
{
    while (lhs.type != VALUE_FAIL) {
        struct value rhs;
        const struct op *op = parse_op(state);
        if (!op)
            break;
        if (op->precedence < min_precedence) {
            untoken(state);
            break;
        }
        rhs = parse_primary(state);
        while (rhs.type != VALUE_FAIL) {
            const struct op *nextop = parse_op(state);
            if (!nextop)
                break;
            untoken(state);
            if (!(nextop->precedence > op->precedence || (nextop->assoc == ASSOC_RIGHT && nextop->precedence == op->precedence)))
                break;
            rhs = parse_expr_rhs(state, rhs, nextop->precedence);
        }
        if (rhs.type == VALUE_FAIL)
            return fail;
        lhs = op->eval(lhs, rhs, op->extra);
    }
    return lhs;
}

static struct value parse_expr(struct parser_state *state)
{
    struct value lhs;
    lhs = parse_primary(state);
    if (lhs.type == VALUE_FAIL)
        return fail;
    return parse_expr_rhs(state, lhs, 0);
}

bool c_expr(int argc, char *argv[], U64 * result)
{
    struct value value;
    struct parser_state state = { .argc = argc, .argv = argv, .arg = argc ? *argv : NULL };

    value = eval_var(parse_expr(&state));
    if (value.type == VALUE_FAIL)
        return false;
    else {
        if (result)
            *result = value.val;
        return true;
    }
}
