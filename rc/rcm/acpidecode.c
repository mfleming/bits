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

#include "portable.h"
#include "acpi.h"
#include "acpidecode.h"

static U8 *parse_acpi_dataobject(const struct acpi_namespace *ns, U8 * current, U8 * end);
static U8 *parse_acpi_package(const struct acpi_namespace *ns, U8 * current, U8 * end);
static U8 *parse_acpi_termarg(const struct acpi_namespace *ns, U8 * current, U8 * end);
static U8 *parse_acpi_termarglist(const struct acpi_namespace *ns, U8 * current, U8 * end);
static U8 *parse_acpi_objectlist(const struct acpi_namespace *ns, U8 * current, U8 * end);

void *decodeTableHeader(void *current, ACPI_TABLE_HEADER ** tableHeader)
{
    *tableHeader = current;
    current = *tableHeader + 1;
    return current;
}

void dprint_nameseg(U32 i)
{
    dprintf("rcm_acpi", "%c%c%c%c",
            (int)(i & 0x000000ff),
            (int)((i & 0x0000ff00) >> 8),
            (int)((i & 0x00ff0000) >> 16),
            (int)(i >> 24));
}

static void dprint_namespace(const struct acpi_namespace *ns)
{
    U32 i;
    dprintf("rcm_acpi", "\\");
    for (i = 0; i < ns->depth; i++) {
        if (i != 0)
            dprintf("rcm_acpi", ".");
        dprint_nameseg(ns->nameseg[i]);
    }
}

static void parsePackageLength(U8 * current, U32 * length, U32 * lengthEncoding)
{
    U32 i;
    U8 len0 = *current;
    U8 numBytes = len0 >> 6;
    U32 total = 0;

    for (i = numBytes; i > 0; i--) {
        total <<= 8;
        total |= current[i];
    }

    total <<= 4;
    total |= len0 & 0x3f;
    *length = total;
    *lengthEncoding = numBytes + 1;
    dprintf("rcm_acpi", "Package length=0x%02x\n", *length);
}

static bool ns_match(struct acpi_namespace *ns1, struct acpi_namespace *ns2)
{
    U32 i;
    if (ns1->depth != ns2->depth)
        return false;

    for (i = 0; i < ns1->depth; i++)
        if (ns1->nameseg[i] != ns2->nameseg[i])
            return false;

    return true;
}

U32 acpi_ns_found;

static U8 *parse_acpi_namestring(const struct acpi_namespace *ns_context, struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;
    struct acpi_namespace dummy_ns;

    (void)end;

    if (!ns)
        ns = &dummy_ns;
    *ns = *ns_context;

    if (*current == AML_ROOT_PREFIX) {
        ns->depth = 0;
        current++;
    } else
        while (*current == AML_PARENT_PREFIX) {
            if (ns->depth == 0) {
                dprintf("rcm_acpi", "Attempt to use parent prefix with no namespace left\n");
                return temp;
            }
            current++;
            ns->depth--;
        }

    switch (*current) {
    case AML_DUAL_NAME_PREFIX:
        if (ns->depth + 2 > ACPI_NAMESPACE_MAX_DEPTH) {
            dprintf("rcm_acpi", "Namespace got too deep\n");
            return temp;
        }
        current++;
        ns->nameseg[ns->depth++] = *(U32 *) current;
        current += 4;
        ns->nameseg[ns->depth++] = *(U32 *) current;
        current += 4;
        break;
    case AML_MULTI_NAME_PREFIX:
        {
            U8 nameseg_count;
            current++;
            nameseg_count = *current++;
            if (ns->depth + nameseg_count > ACPI_NAMESPACE_MAX_DEPTH) {
                dprintf("rcm_acpi", "Namespace got too deep\n");
                return temp;
            }
            while (nameseg_count--) {
                ns->nameseg[ns->depth++] = *(U32 *) current;
                current += 4;
            }
            break;
        }
    case AML_NULL_NAME:
        current++;
        break;
    default:
        if (*current != '_' && (*current < 'A' || *current > 'Z')) {
            dprintf("rcm_acpi", "Invalid nameseg lead character: 0x%02x\n", *current);
            return temp;
        }
        if (ns->depth + 1 > ACPI_NAMESPACE_MAX_DEPTH) {
            dprintf("rcm_acpi", "Namespace got too deep\n");
            return temp;
        }
        ns->nameseg[ns->depth++] = *(U32 *) current;
        current += 4;
        break;
    }

    dprintf("rcm_acpi", "Found NameString: ");
    dprint_namespace(ns);
    dprintf("rcm_acpi", "\n");

    if (!acpi_ns_found) {
        U32 index;

        for (index = 0; index < acpi_processor_count; index++)
            if (ns_match(ns, &acpi_processors[index].ns)) {
                acpi_ns_found = 1;
                break;
            }
    }
    return current;
}

static U8 *parse_acpi_buffer(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U32 pkglen, lengthEncoding;
    (void)ns;
    (void)end;
    if (*current != AML_BUFFER_OP)
        return current;
    current++;
    parsePackageLength(current, &pkglen, &lengthEncoding);
    current += pkglen;
    return current;
}

static U8 *parse_acpi_computationaldata(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    current = parse_acpi_buffer(ns, current, end);
    if (current != temp)
        return current;

    switch (*current) {
    case AML_BYTE_OP:
        dprintf("rcm_acpi", "Found ByteOp\n");
        current += 1 + 1;
        break;
    case AML_WORD_OP:
        dprintf("rcm_acpi", "Found WordOp\n");
        current += 1 + 2;
        break;
    case AML_DWORD_OP:
        dprintf("rcm_acpi", "Found DwordOp\n");
        current += 1 + 4;
        break;
    case AML_QWORD_OP:
        dprintf("rcm_acpi", "Found QwordOp\n");
        current += 1 + 8;
        break;
    case AML_STRING_OP:
        dprintf("rcm_acpi", "Found StringOp: \"");
        current++;
        while (*current)
            if (*current < ' ' || *current > 0x7e)
                dprintf("rcm_acpi", "\\x%02x", *current++);
            else
                dprintf("rcm_acpi", "%c", *current++);
        current++; /* Skip the \0 */
        dprintf("rcm_acpi", "\"\n");
        break;
    case AML_ZERO_OP:
        dprintf("rcm_acpi", "Found ZeroOp\n");
        current += 1;
        break;
    case AML_ONE_OP:
        dprintf("rcm_acpi", "Found OneOp\n");
        current += 1;
        break;
    case AML_ONES_OP:
        dprintf("rcm_acpi", "Found OneOp\n");
        current += 1;
        break;
    case AML_EXT_OP_PREFIX:
        if (*(current + 1) == AML_REVISION_OP)
            current += 2;
    default:
        break;
    }

    return current;
}

static U8 *parse_acpi_argobj(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    (void)ns;
    (void)end;
    switch (*current) {
    case AML_ARG0_OP:
        dprintf("rcm_acpi", "Found Arg0Op\n");
        current++;
        break;
    case AML_ARG1_OP:
        dprintf("rcm_acpi", "Found Arg1Op\n");
        current++;
        break;
    case AML_ARG2_OP:
        dprintf("rcm_acpi", "Found Arg2Op\n");
        current++;
        break;
    case AML_ARG3_OP:
        dprintf("rcm_acpi", "Found Arg3Op\n");
        current++;
        break;
    case AML_ARG4_OP:
        dprintf("rcm_acpi", "Found Arg4Op\n");
        current++;
        break;
    case AML_ARG5_OP:
        dprintf("rcm_acpi", "Found Arg5Op\n");
        current++;
        break;
    case AML_ARG6_OP:
        dprintf("rcm_acpi", "Found Arg6Op\n");
        current++;
        break;
    default:
        break;
    }
    return current;
}

static U8 *parse_acpi_localobj(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    (void)ns;
    (void)end;
    switch (*current) {
    case AML_LOCAL0_OP:
        dprintf("rcm_acpi", "Found Local0Op\n");
        current++;
        break;
    case AML_LOCAL1_OP:
        dprintf("rcm_acpi", "Found Local1Op\n");
        current++;
        break;
    case AML_LOCAL2_OP:
        dprintf("rcm_acpi", "Found Local2Op\n");
        current++;
        break;
    case AML_LOCAL3_OP:
        dprintf("rcm_acpi", "Found Local3Op\n");
        current++;
        break;
    case AML_LOCAL4_OP:
        dprintf("rcm_acpi", "Found Local4Op\n");
        current++;
        break;
    case AML_LOCAL5_OP:
        dprintf("rcm_acpi", "Found Local5Op\n");
        current++;
        break;
    case AML_LOCAL6_OP:
        dprintf("rcm_acpi", "Found Local6Op\n");
        current++;
        break;
    case AML_LOCAL7_OP:
        dprintf("rcm_acpi", "Found Local7Op\n");
        current++;
        break;
    default:
        break;
    }
    return current;
}

static U8 *parse_acpi_debugobj(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    (void)ns;
    (void)end;
    if ((*current == AML_EXT_OP_PREFIX) && (*(current + 1) == AML_DEBUG_OP)) {
        current += 2;
        dprintf("rcm_acpi", "Found DebugOp\n");
    }

    return current;
}

static U8 *parse_acpi_datarefobject(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    dprintf("rcm_acpi", "Beginning datarefobject: 0x%02x at memory location %p\n", *current, current);
    current = parse_acpi_dataobject(ns, current, end);
    if (current != temp)
        return current;

    return current;
}

static U8 *parse_acpi_simplename(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    current = parse_acpi_namestring(ns, NULL, current, end);
    if (current != temp)
        return current;

    current = parse_acpi_argobj(ns, current, end);
    if (current != temp)
        return current;

    current = parse_acpi_localobj(ns, current, end);
    if (current != temp)
        return current;

    return current;
}

static U8 *parse_acpi_supername(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    current = parse_acpi_simplename(ns, current, end);
    if (current != temp)
        return current;

    current = parse_acpi_debugobj(ns, current, end);
    if (current != temp)
        return current;

    return current;
}

static U8 *parse_acpi_target(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    current = parse_acpi_supername(ns, current, end);
    if (current != temp)
        return current;

    if (*current == AML_NULL_NAME)
        current++;

    return current;
}

static U8 *parse_acpi_method(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *new_end = current;
    U8 *temp;
    U32 pkglen;
    U32 lengthEncoding;
    struct acpi_namespace new_ns;

    (void)end;

    parsePackageLength(current, &pkglen, &lengthEncoding);
    current += lengthEncoding;
    new_end += pkglen;

    temp = current;
    current = parse_acpi_namestring(ns, &new_ns, current, new_end);
    if (current == temp)
        return new_end;

    dprintf("rcm_acpi", "Found Method: ");
    dprint_namespace(&new_ns);
    dprintf("rcm_acpi", "\n");

    // U8 methodFlags
    current++;

    parse_acpi_termlist(&new_ns, current, new_end);

    dprintf("rcm_acpi", "End of Method: ");
    dprint_namespace(&new_ns);
    dprintf("rcm_acpi", "\n");

    return new_end;
}

U32 acpi_processor_count;
struct acpi_processor acpi_processors[CPU_MAP_LIMIT];

static void add_processor(const struct acpi_namespace *ns, U8 id, U32 pmbase)
{
    if (acpi_processor_count == CPU_MAP_LIMIT) {
        dprintf("rcm_acpi", "No more room for ACPI processor structures\n");
        return;
    }
    acpi_processors[acpi_processor_count].ns = *ns;
    acpi_processors[acpi_processor_count].id = id;
    acpi_processors[acpi_processor_count].pmbase = pmbase;
    acpi_processor_count++;
}

static U8 *parse_acpi_processor(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *new_end = current;
    U8 *temp;
    U32 pkglen;
    U32 lengthEncoding;
    struct acpi_namespace new_ns;
    U8 id;
    U32 pmbase;

    (void)end;

    parsePackageLength(current, &pkglen, &lengthEncoding);
    current += lengthEncoding;
    new_end += pkglen;

    temp = current;
    current = parse_acpi_namestring(ns, &new_ns, current, new_end);
    if (current == temp)
        return new_end;

    id = *current++;
    pmbase = *(U32 *) current;
    current += 4;

    dprintf("rcm_acpi", "Found CPU object: ");
    dprint_namespace(&new_ns);
    dprintf("rcm_acpi", " id = 0x%x pmbase = 0x%x\n", id, pmbase);

    add_processor(&new_ns, id, pmbase);

    return new_end;
}

static U8 *parse_acpi_namedobj(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    dprintf("rcm_acpi", "Beginning namedobj: 0x%02x at memory location %p\n", *current, current);
    switch (*current) {
    case AML_EXT_OP_PREFIX:
        {
            if (*(current + 1) == AML_MUTEX_OP) {
                struct acpi_namespace new_ns;

                current += 2;
                current = parse_acpi_namestring(ns, &new_ns, current, end);
                dprintf("rcm_acpi", "Mutex: ");
                dprint_namespace(&new_ns);
                dprintf("rcm_acpi", "\n");
                current++; /* SyncFlags */
            } else if (*(current + 1) == AML_OPREGION_OP) {
                struct acpi_namespace new_ns;

                current += 2;
                dprintf("rcm_acpi", "OpRegion at memory location %p\n", current);
                current = parse_acpi_namestring(ns, &new_ns, current, end);
                dprintf("rcm_acpi", "OpRegion name: ");
                dprint_namespace(&new_ns);
                dprintf("rcm_acpi", "\n");
                current++;
                current = parse_acpi_termarg(ns, current, end);
                current = parse_acpi_termarg(ns, current, end);
                dprintf("rcm_acpi", "End OpRegion: ");
                dprint_namespace(&new_ns);
                dprintf("rcm_acpi", "\n");
            } else if (*(current + 1) == AML_FIELD_OP) {
                U32 pkglen;
                U32 lengthEncoding;

                current += 2;
                dprintf("rcm_acpi", "FieldOp at memory location %p\n", current);
                parsePackageLength(current, &pkglen, &lengthEncoding);
                current += pkglen;
            } else if (*(current + 1) == AML_DEVICE_OP) {
                U8 *new_end;
                U32 pkglen;
                U32 lengthEncoding;
                struct acpi_namespace new_ns;

                current += 2;
                new_end = current;
                dprintf("rcm_acpi", "DeviceOp at memory location %p\n", current);
                parsePackageLength(current, &pkglen, &lengthEncoding);
                current += lengthEncoding;
                new_end += pkglen;
                current = parse_acpi_namestring(ns, &new_ns, current, new_end);
                dprintf("rcm_acpi", "DeviceOp name: ");
                dprint_namespace(&new_ns);
                dprintf("rcm_acpi", "\n");

                current = parse_acpi_objectlist(&new_ns, current, new_end);
                current = new_end;
            } else if (*(current + 1) == AML_PROCESSOR_OP) {
                current += 2;
                current = parse_acpi_processor(ns, current, end);
            } else if (*(current + 1) == AML_INDEXFIELD_OP) {
                U8 *new_end;
                U32 pkglen;
                U32 lengthEncoding;
                struct acpi_namespace new_ns;

                current += 2;
                new_end = current;
                dprintf("rcm_acpi", "IndexFieldOp at memory location %p\n", current);
                parsePackageLength(current, &pkglen, &lengthEncoding);
                current += lengthEncoding;
                new_end += pkglen;
                current = parse_acpi_namestring(ns, &new_ns, current, new_end);
                dprintf("rcm_acpi", "IndexFieldOp name: ");
                dprint_namespace(&new_ns);
                dprintf("rcm_acpi", "\n");

                current = parse_acpi_objectlist(&new_ns, current, new_end);
                current = new_end;
            }
            break;
        }
    case AML_METHOD_OP:
        {
            current++;
            current = parse_acpi_method(ns, current, end);
            break;
        }
    default:
        break;
    }
    return current;
}

static U8 *parse_acpi_type1opcode(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    dprintf("rcm_acpi", "Beginning type1opcode: 0x%02x at memory location %p\n", *current, current);
    switch (*current) {
    case AML_IF_OP:
        {
            U8 *new_end;
            U32 pkgLen;
            U32 lengthEncoding;

            dprintf("rcm_acpi", "Found IfOp\n");
            current++;
            parsePackageLength(current, &pkgLen, &lengthEncoding);
            new_end = current + pkgLen;
            current += lengthEncoding;

            current = parse_acpi_termarg(ns, current, new_end);
            parse_acpi_termlist(ns, current, new_end);
            current = new_end;
            break;
        }
    case AML_ELSE_OP:
        {
            U8 *new_end;
            U32 pkgLen;
            U32 lengthEncoding;

            dprintf("rcm_acpi", "Found ElseOp\n");
            current++;
            parsePackageLength(current, &pkgLen, &lengthEncoding);
            new_end = current + pkgLen;
            current += lengthEncoding;

            parse_acpi_termlist(ns, current, new_end);
            current = new_end;
            break;
        }
    case AML_RETURN_OP:
        {
            dprintf("rcm_acpi", "Found ReturnOp\n");
            current++;
            current = parse_acpi_termarg(ns, current, end);
            break;
        }
    default:
        break;
    }
    return current;
}

static U8 *parse_acpi_type2opcode(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    dprintf("rcm_acpi", "Beginning type2opcode: 0x%02x at memory location %p\n", *current, current);

    current = parse_acpi_package(ns, current, end);
    if (current != temp)
        return current;

    switch (*current) {
    case AML_LNOT_OP:
        current++;
        dprintf("rcm_acpi", "Found logical not operator\n");
        current = parse_acpi_termarg(ns, current, end);
        break;

    case AML_LAND_OP:
    case AML_LOR_OP:
    case AML_LEQUAL_OP:
    case AML_LGREATER_OP:
    case AML_LLESS_OP:
        dprintf("rcm_acpi", "Found logical binary operator: %c\n", "&|!=><"[*current - AML_LAND_OP]);
        current++;
        current = parse_acpi_termarg(ns, current, end);
        current = parse_acpi_termarg(ns, current, end);
        break;

    case AML_EXT_OP_PREFIX:
        {
            if (*(current + 1) == AML_COND_REF_OF_OP) {
                dprintf("rcm_acpi", "Found CondRefOf\n");
                current += 2;
                current = parse_acpi_supername(ns, current, end);
                current = parse_acpi_target(ns, current, end);
            }
            break;
        }
    case AML_STORE_OP:
        {
            dprintf("rcm_acpi", "Found StoreOp\n");
            current++;
            current = parse_acpi_termarg(ns, current, end);
            current = parse_acpi_supername(ns, current, end);
            break;
        }
    default:
        {
            current = parse_acpi_namestring(ns, NULL, current, end);
            if (current == temp)
                break;
            current = parse_acpi_termarglist(ns, current, end);
        }
    }
    return current;
}

static U8 *parse_acpi_package(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    (void)ns;
    (void)end;
    if (*current == AML_PACKAGE_OP) {
        U32 pkglen;
        U32 lengthEncoding;

        dprintf("rcm_acpi", "Found PackageOp\n");
        current++;
        parsePackageLength(current, &pkglen, &lengthEncoding);
        current += pkglen;
    }
    return current;
}

static U8 *parse_acpi_dataobject(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    current = parse_acpi_computationaldata(ns, current, end);
    if (current != temp)
        return current;

    current = parse_acpi_package(ns, current, end);
    if (current != temp)
        return current;

    return current;
}

static U8 *parse_acpi_termarg(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    U8 *temp = current;

    dprintf("rcm_acpi", "Beginning termarg: 0x%02x at memory location %p\n", *current, current);

    current = parse_acpi_type2opcode(ns, current, end);
    if (current != temp)
        return current;

    current = parse_acpi_dataobject(ns, current, end);
    if (current != temp)
        return current;

    current = parse_acpi_argobj(ns, current, end);
    if (current != temp)
        return current;

    current = parse_acpi_localobj(ns, current, end);
    if (current != temp)
        return current;

    return current;
}

static U8 *parse_acpi_namespacemodifierobj(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    dprintf("rcm_acpi", "Beginning namespacemodifierobj: 0x%02x at memory location %p\n", *current, current);
    switch (*current) {
    case AML_SCOPE_OP:
        {
            U8 *new_end;
            struct acpi_namespace new_ns;
            U32 scopeLen;
            U32 lengthEncoding;

            current++;
            parsePackageLength(current, &scopeLen, &lengthEncoding);
            new_end = current + scopeLen;

            current = parse_acpi_namestring(ns, &new_ns, current + lengthEncoding, new_end);

            dprintf("rcm_acpi", "Found Scope: ");
            dprint_namespace(&new_ns);
            dprintf("rcm_acpi", "\n");

            parse_acpi_termlist(&new_ns, current, new_end);

            dprintf("rcm_acpi", "End Scope: ");
            dprint_namespace(&new_ns);
            dprintf("rcm_acpi", "\n");

            current = new_end;
            break;
        }
    case AML_NAME_OP:
        current++;
        current = parse_acpi_namestring(ns, NULL, current, end);
        current = parse_acpi_datarefobject(ns, current, end);
        break;
    case AML_ALIAS_OP:
        current++;
        current = parse_acpi_namestring(ns, NULL, current, end);
        current = parse_acpi_namestring(ns, NULL, current, end);
        break;
    default:
        break;
    }
    return current;
}

static U8 *parse_acpi_objectlist(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    dprintf("rcm_acpi", "Beginning objectlist: 0x%02x at memory location %p end=%p\n", *current, current, end);
    while (current < end) {
        U8 *temp = current;

        dprintf("rcm_acpi", "New iteration of objectlist: 0x%02x at memory location %p end=%p\n", *current, current, end);

        current = parse_acpi_namespacemodifierobj(ns, current, end);
        if (current != temp)
            continue;

        current = parse_acpi_namedobj(ns, current, end);
        if (current != temp)
            continue;

        if (current == temp) {
            dprintf("rcm_acpi", "Unhandled object in object list: 0x%02x at memory location %p\n", *current, current);
            dprintf("rcm_acpi", "namespace:  ");
            dprint_namespace(ns);
            dprintf("rcm_acpi", "\n");
            break;
        }
    }
    dprintf("rcm_acpi", "Ending objectlist: 0x%02x at memory location %p\n", *current, current);
    return current;
}

static U8 *parse_acpi_termarglist(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    dprintf("rcm_acpi", "Beginning termarglist: 0x%02x at memory location %p\n", *current, current);
    while (current < end) {
        U8 *temp = current;

        current = parse_acpi_termarg(ns, current, end);
        if (current == temp) {
            dprintf("rcm_acpi", "Unhandled item in term arg list: 0x%02x at memory location %p\n", *current, current);
            dprintf("rcm_acpi", "namespace:  ");
            dprint_namespace(ns);
            dprintf("rcm_acpi", "\n");
            break;
        }
    }
    return current;
}

void parse_acpi_termlist(const struct acpi_namespace *ns, U8 * current, U8 * end)
{
    while (current < end) {
        U8 *temp = current;

        dprintf("rcm_acpi", "Beginning new term in term list: 0x%02x at memory location %p\n", *current, current);

        current = parse_acpi_namespacemodifierobj(ns, current, end);
        if (current != temp)
            continue;

        current = parse_acpi_namedobj(ns, current, end);
        if (current != temp)
            continue;

        current = parse_acpi_type1opcode(ns, current, end);
        if (current != temp)
            continue;

        current = parse_acpi_type2opcode(ns, current, end);
        if (current != temp)
            continue;

        switch (*current) {
        default:
            {
                dprintf("rcm_acpi", "Unhandled item in term list: 0x%02x at memory location %p\n", *current, current);
                dprintf("rcm_acpi", "namespace:  ");
                dprint_namespace(ns);
                dprintf("rcm_acpi", "\n");
                return;
            }
        }
    }
}
