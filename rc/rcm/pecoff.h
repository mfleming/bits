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

#pragma pack(1)

typedef struct {
    U16 u16Machine;
    U16 u16NumSections;
    U32 u32DateTimeStamp;
    U32 u32SymTblPtr;
    U32 u32NumSymbols;
    U16 u16PeHdrSize;
    U16 u16Characs;
} COFFHDR, *PCOFFHDR;

typedef struct {
    U16 u16MagicNum;
    U8 u8LinkerMajVer;
    U8 u8LinkerMinVer;
    U32 u32CodeSize;
    U32 u32IDataSize;
    U32 u32UDataSize;
    U32 u32EntryPoint;
    U32 u32CodeBase;
    U32 u32DataBase;
    U32 u32ImgBase;
    U32 u32SectionAlignment;
    U32 u32FileAlignment;
    U16 u16OSMajVer;
    U16 u16OSMinVer;
    U16 u16ImgMajVer;
    U16 u16ImgMinVer;
    U16 u16SubMajVer;
    U16 u16SubMinVer;
    U32 u32Rsvd;
    U32 u32ImgSize;
    U32 u32HdrSize;
    U32 u32Chksum;
    U16 u16Subsystem;
    U16 u16DLLChars;
    U32 u32StkRsrvSize;
    U32 u32StkCmmtSize;
    U32 u32HeapRsrvSize;
    U32 u32HeapCmmtSize;
    U32 u32LdrFlags;
    U32 u32NumDatDirs;
} PEHDR, *PPEHDR;

typedef struct {
    U32 u32Rva;
    U32 u32DataDirSize;
} DATADIR, *PDATADIR;

typedef struct {
    char cName[8];
    U32 u32VirtualSize;
    U32 u32VirtualAddress;
    U32 u32RawDataSize;
    U32 u32RawDataPtr;
    U32 u32RelocPtr;
    U32 u32LineNumPtr;
    U16 u16NumRelocs;
    U16 u16NumLineNums;
    U32 u32Characs;
} SECTIONTBL, *PSECTIONTBL;
