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

#include <grub/misc.h>
#define strcmp grub_strcmp

#include "datatype.h"
#include "pecoff.h"
#include "placepe.h"

#define dvprintf(fmt, ...) do { } while(0)

static void fUpdateImageBase(void *buffer, U32 base_address, U32 PeSignOffset, PEHDR * PeHdr)
{
    U8 *ptr = buffer;
    long lOffset;

    lOffset = PeSignOffset + 4 + sizeof(COFFHDR) + (long)&PeHdr->u32ImgBase - (long)&PeHdr->u16MagicNum;
    ptr += lOffset;
    *(U32 *) ptr = base_address;
}

static void fApplyFixupDelta(void *buffer, U32 u32Delta, U32 u16Offset, U8 u8Type)
{
    void *ptr;
    U32 u32Data;

    ptr = (U8 *) buffer + u16Offset;
    u32Data = *(U32 *) ptr; /* Intentionally don't move ptr forward. */

    switch (u8Type) {
    case 0:
        break;
    case 1:
        u32Data += (u32Delta >> 16) & 0x0000ffff;
        break;
    case 2:
        u32Data += u32Delta & 0x0000ffff;
        break;
    case 3:
        u32Data += u32Delta;
        break;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    default:
        break;
    }

    *(U32 *) ptr = u32Data;
}

static void fProcessRelocSection(void *buffer, U32 base_address, U32 RelocSectionOffset, U32 RelocSectionVirtSize, PEHDR * PeHdr)
{
    void *ptr;
    U32 i;
    U32 u32FixupDelta;
    U32 u32PageRva;
    U32 u32BlockSize;
    U16 u16TypeOffset;
    U8 u8Type;
    U16 u16Offset;
    U32 u32Size;

    // Calculate the fixup delta.
    u32FixupDelta = base_address - PeHdr->u32ImgBase;

    // Seek to the start of the .reloc section.
    ptr = (U8 *) buffer + RelocSectionOffset;

    u32Size = RelocSectionVirtSize;

    // This seems to be a bug in the way MS generates the reloc fixup blocks.
    // After we have gone thru all the fixup blocks in the .reloc section, the
    // variable u32Size should ideally go to zero. But I have found some orphan
    // data after all the fixup blocks that don't quite fit anywhere. So, I have
    // changed the check to a greater-than-eight. It should be at least eight
    // because the PageRva and the BlockSize together take eight bytes. If less
    // than 8 are remaining, then those are the orphans and we need to disregard them.
    while (u32Size >= 8) {
        dvprintf("(placepe) u32Size = %08lx\n", u32Size);

        // Read the Page RVA and Block Size for the current fixup block.
        u32PageRva = *(U32 *) ptr;
        ptr = ((U32 *) ptr) + 1;
        u32BlockSize = *(U32 *) ptr;
        ptr = ((U32 *) ptr) + 1;

        if (u32BlockSize == 0) {
            dvprintf("u32BlockSize is 0, breaking out of while()...\n");
            break;
        }

        u32Size -= sizeof(U32) * 2;

        // Extract the correct number of Type/Offset entries. This is given by:
        // Loop count = Number of relocation items =
        // (Block Size - 4 bytes (Page RVA field) - 4 bytes (Block Size field)) divided
        // by 2 (each Type/Offset entry takes 2 bytes).
        dvprintf("LoopCount = %04lx\n", ((u32BlockSize - 2 * sizeof(U32)) / sizeof(U16)));

        for (i = 0; i < ((u32BlockSize - 2 * sizeof(U32)) / sizeof(U16)); i++) {
            u16TypeOffset = *(U16 *) ptr;
            ptr = ((U16 *) ptr) + 1;

            u8Type = (U8) ((u16TypeOffset & 0xf000) >> 12);
            u16Offset = (U16) ((U16) u16TypeOffset & 0x0fff);
            u32Size -= sizeof(U16);

            dvprintf("%04lx: PageRva: %08lx Offset: %04x Type: %x \n", i, u32PageRva, u16Offset, u8Type);

            fApplyFixupDelta(buffer, u32FixupDelta, (u32PageRva + (U32) u16Offset), u8Type);
        }
    }
}

void *placepe(void *buffer, U32 base_address)
{
    void *ptr;
    U32 i;

    U32 PeSignOffset;
    COFFHDR CoffHdr;
    PEHDR PeHdr;
    SECTIONTBL SectionTbl;

    // --------------------------------------------------------
    // Get the PE Signature offset from the MS-DOS header.
    ptr = (U8 *) buffer + 0x3c;
    PeSignOffset = *(U32 *) ptr;
    ptr = (U8 *) buffer + PeSignOffset + 4;

    CoffHdr = *(COFFHDR *) ptr;
    ptr = ((COFFHDR *) ptr) + 1;
    PeHdr = *(PEHDR *) ptr;
    ptr = ((PEHDR *) ptr) + 1;

    ptr = (DATADIR *)ptr + PeHdr.u32NumDatDirs;

    // Read as many sections as are indicated by the COFF header.
    for (i = 0; i < CoffHdr.u16NumSections; i++) {
        SectionTbl = *(SECTIONTBL *) ptr;
        ptr = ((SECTIONTBL *) ptr) + 1;

        if (strcmp(SectionTbl.cName, ".reloc") == 0) {
            //  Got the .reloc section!
            fProcessRelocSection(buffer, base_address, SectionTbl.u32RawDataPtr, SectionTbl.u32VirtualSize, &PeHdr);
        }
    }

    // Finally, modify the image base field of the PE Header of the output file.
    fUpdateImageBase(buffer, base_address, PeSignOffset, &PeHdr);

    return (void *)(base_address + PeHdr.u32EntryPoint);
}
