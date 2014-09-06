# Copyright (c) 2013, Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#     * Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright notice,
#       this list of conditions and the following disclaimer in the documentation
#       and/or other materials provided with the distribution.
#     * Neither the name of Intel Corporation nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Give a presentation using EFI Graphics Output Protocol (GOP)."""

import bits
import ctypes
import efi
import itertools
import os
import readline
import zlib

PixelRedGreenBlueReserved8BitPerColor, PixelBlueGreenRedReserved8BitPerColor, PixelBitMask, PixelBltOnly = range(4)

EfiBltVideoFill, EfiBltVideoToBltBuffer, EfiBltBufferToVideo, EfiBltVideoToVideo = range(4)

class GraphicsOutputModeInformation(efi.Struct):
    """EFI Graphics Output Mode Information"""
    _fields_ = [
        ('Version', ctypes.c_uint32),
        ('HorizontalResolution', ctypes.c_uint32),
        ('VerticalResolution', ctypes.c_uint32),
        ('PixelFormat', ctypes.c_uint32),
        ('RedMask', ctypes.c_uint32),
        ('GreenMask', ctypes.c_uint32),
        ('BlueMask', ctypes.c_uint32),
        ('ReservedMask', ctypes.c_uint32),
        ('PixelsPerScanLine', ctypes.c_uint32),
    ]

class GraphicsOutputProtocolMode(efi.Struct):
    """EFI Graphics Output Protocol Mode"""
    _fields_ = [
        ('MaxMode', ctypes.c_uint32),
        ('Mode', ctypes.c_uint32),
        ('Info', ctypes.POINTER(GraphicsOutputModeInformation)),
        ('SizeOfInfo', ctypes.c_ulong),
        ('FrameBufferBase', ctypes.c_uint64),
        ('FrameBufferSize', ctypes.c_ulong),
    ]

class GraphicsOutputProtocol(efi.Protocol):
    """EFI Graphics Output Protocol"""
    guid = efi.EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID
    _fields_ = [
        ('QueryMode', ctypes.c_void_p),
        ('SetMode', ctypes.c_void_p),
        ('Blt', ctypes.c_void_p),
        ('Mode', ctypes.POINTER(GraphicsOutputProtocolMode)),
    ]

def init():
    global gop
    gop = GraphicsOutputProtocol.from_handle(efi.system_table.ConsoleOutHandle)

def load(name="slides", bufsize=1024):
    """Load presentation slides"""
    global slides, current_slide, max_slide, saved_screen
    init()
    info = gop.Mode.contents.Info.contents
    saved_screen = ctypes.create_string_buffer(info.HorizontalResolution * info.VerticalResolution * 4)
    directory = os.path.join("/", name, "{}x{}".format(info.HorizontalResolution, info.VerticalResolution))
    slides = []
    for n in itertools.count(1):
        try:
            f = open(os.path.join(directory, "{}z".format(n)), "rb")
            s = ''.join(iter(lambda:f.read(bufsize), ''))
            s = zlib.decompress(s)
        except IOError:
            max_slide = n - 2
            break
        slides.append(ctypes.create_string_buffer(s, len(s)))
    current_slide = 0
    sizes = list(len(f) for f in slides)
    if set(sizes) != set([info.HorizontalResolution * info.VerticalResolution * 4]):
        print "Load error: Inconsistent buffer sizes = {}".format(sizes)
    readline.add_key_hook(bits.KEY_F10, resume)

def resume():
    """Start or resume a presentation"""
    global slides, current_slide, max_slide, saved_screen
    info = gop.Mode.contents.Info.contents
    efi.call(gop.Blt, ctypes.addressof(gop), ctypes.addressof(saved_screen), EfiBltVideoToBltBuffer, 0, 0, 0, 0, info.HorizontalResolution, info.VerticalResolution, 0)
    while True:
        if len(slides[current_slide]) != info.HorizontalResolution * info.VerticalResolution * 4:
            break
        efi.call(gop.Blt, ctypes.addressof(gop), ctypes.addressof(slides[current_slide]), EfiBltBufferToVideo, 0, 0, 0, 0, info.HorizontalResolution, info.VerticalResolution, 0)
        k = bits.get_key()
        if k == bits.KEY_ESC:
            break
        elif k in (bits.KEY_LEFT, bits.KEY_UP, bits.KEY_PAGE_UP):
            if current_slide > 0:
                current_slide -= 1
        elif k in (bits.KEY_RIGHT, bits.KEY_DOWN, bits.KEY_PAGE_DOWN, ord(' ')):
            if current_slide < max_slide:
                current_slide += 1
        elif k == bits.KEY_HOME:
            current_slide = 0
    efi.call(gop.Blt, ctypes.addressof(gop), ctypes.addressof(saved_screen), EfiBltBufferToVideo, 0, 0, 0, 0, info.HorizontalResolution, info.VerticalResolution, 0)
