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

"""bits module."""

import _bits
from _bits import *
import _smp
from _smp import *
import itertools
from collections import namedtuple
import string
import struct
import time

MOD_SHIFT = 0x01000000
MOD_CTRL = 0x02000000
MOD_ALT = 0x04000000

KEY_ESC = 0x1b
KEY_LEFT = 0x0080004b
KEY_RIGHT = 0x0080004d
KEY_UP = 0x00800048
KEY_DOWN = 0x00800050
KEY_HOME = 0x00800047
KEY_END = 0x0080004f
KEY_DELETE = 0x00800053
KEY_PAGE_UP = 0x00800049
KEY_PAGE_DOWN = 0x00800051
KEY_F1 = 0x0080003b
KEY_F2 = 0x0080003c
KEY_F3 = 0x0080003d
KEY_F4 = 0x0080003e
KEY_F5 = 0x0080003f
KEY_F6 = 0x00800040
KEY_F7 = 0x00800041
KEY_F8 = 0x00800042
KEY_F9 = 0x00800043
KEY_F10 = 0x00800044
KEY_F11 = 0x00800057
KEY_F12 = 0x00800058
KEY_INSERT = 0x00800052
KEY_CENTER = 0x0080004c

ptrsize = struct.calcsize("P")

class cpuid_result(namedtuple('cpuid_result', ['eax', 'ebx', 'ecx', 'edx'])):
    __slots__ = ()

    def __repr__(self):
        return "cpuid_result(eax={eax:#010x}, ebx={ebx:#010x}, ecx={ecx:#010x}, edx={edx:#010x})".format(**self._asdict())

def cpuid(apicid, eax, ecx=0):
    """Run CPUID on the specified CPU. Return a namedtuple containing eax, ebx, ecx, and edx."""
    return cpuid_result(*_smp._cpuid(apicid, eax, ecx))

_grub_command_map = {}

def addr_alignment(addr):
    """Compute the maximum alignment of a specified address, up to 4."""
    return addr & 1 or addr & 2 or 4

PCI_ADDR_PORT = 0xCF8
PCI_DATA_PORT = 0xCFC

def _pci_op(bus, device, function, register, bytes):
    if bytes is None:
        bytes = addr_alignment(register)
    elif bytes not in [1,2,4]:
        raise ValueError("bytes must be 1, 2, or 4")
    outl(PCI_ADDR_PORT, ((1 << 31) | (bus << 16) | (device << 11) | (function << 8) | register) & ~3)
    return bytes, PCI_DATA_PORT + (register & 3)

def pci_read(bus, device, function, register, bytes=None):
    """Read a value of the specified size from the PCI device specified by bus:device.function register"""
    bytes, port = _pci_op(bus, device, function, register, bytes)
    return { 1: inb, 2: inw, 4: inl }[bytes](port)

def pci_write(bus, device, function, register, value, bytes=None):
    """Write a value of the specified size to the PCI device specified by bus:device.function register"""
    bytes, port = _pci_op(bus, device, function, register, bytes)
    { 1: outb, 2: outw, 4: outl }[bytes](port, value)

_pcie_base = None

def pcie_get_base():
    return _pcie_base

def pcie_set_base(memaddr):
    global _pcie_base
    if memaddr < 0 or memaddr >= 2**32:
        raise ValueError("PCIe base address out of supported range; must be below 4GB")
    _pcie_base = memaddr

def _pcie_op(bus, device, function, register, bytes, memaddr):
    if bytes is None:
        bytes = addr_alignment(register)
    elif bytes not in [1,2,4,8]:
        raise ValueError("bytes must be 1, 2, 4, or 8")
    if memaddr is None:
        memaddr = _pcie_base
        if memaddr is None:
            raise ValueError("PCIe base address not set")
    elif memaddr < 0 or memaddr >= 2**32:
        raise ValueError("PCIe base address out of supported range; must be below 4GB")
    return bytes, memaddr | (bus << 20) | (device << 15) | (function << 12) | register

def pcie_read(bus, device, function, register, bytes=None, memaddr=None):
    """Read a value of the specified size from the PCIe device specified by bus:device.function register"""
    bytes, addr = _pcie_op(bus, device, function, register, bytes, memaddr)
    return { 1: readb, 2: readw, 4: readl, 8: readq }[bytes](addr)

def pcie_write(bus, device, function, register, value, bytes=None, memaddr=None):
    """Write a value of the specified size to the PCIe device specified by bus:device.function register"""
    bytes, addr = _pcie_op(bus, device, function, register, bytes, memaddr)
    { 1: writeb, 2: writew, 4: writel, 8: writeq }[bytes](addr, value)

def register_grub_command(command, func, summary, description):
    """Register a new GRUB command, implemented using the given callable.
    The callable should accept a single argument, the list of argument strings.
    The arguments include the command name as [0]."""
    _bits._register_grub_command(command, summary, description)
    _grub_command_map[command] = func

def _grub_command_callback(args):
    try:
        return _grub_command_map[args[0]](args)
    except:
        import traceback
        traceback.print_exc()
        return False

_bits._set_grub_command_callback(_grub_command_callback)

# Cache the cpulist since it will never change.
cpulist = cpus()

def cpus():
    """cpus() -> list of APIC IDs."""
    return cpulist

def bsp_apicid():
    """Returns the BSP's APIC ID."""
    global cpulist
    return cpulist[0]

def socket_index(apic_id):
    """Returns the socket portion of the APIC ID"""
    if apic_id is None:
        return None
    if cpuid(apic_id,0x0)[0] < 0xb:
        return None
    eax, ebx, ecx, edx = cpuid(apic_id,0xb,1)
    x2apic_id = edx
    socket_index = x2apic_id >> (eax & 0x1f);
    return socket_index

def socket_apic_ids():
    """Returns a mapping to unique socket index with the list of APIC IDs"""
    global cpulist
    uniques = {}
    for apicid in sorted(cpulist):
        uniques.setdefault(socket_index(apicid), []).append(apicid)
    return uniques

def apicid_to_index():
    """Returns a reverse mapping from APIC ID to CPU number"""
    global cpulist
    return dict([(apicid, i) for (i, apicid) in enumerate(cpulist)])

def cpu_frequency():
    global cpulist
    IA32_MPERF_MSR = 0xE7
    IA32_APERF_MSR = 0xE8

    if cpuid(bsp_apicid(), 0).eax < 6:
        # CPUID Leaf 6 is not supported
        return None

    if cpuid(bsp_apicid(), 6).ecx & 1 == 0:
        # MPERF/APERF MSRs are not supported
        return None

    for apicid in cpulist:
        set_mwait(apicid, True, 0x20)

    if wrmsr(bsp_apicid(), IA32_MPERF_MSR, 0) is None:
        # Writing of IA32_MPERF MSR caused a GPF
        return None

    if wrmsr(bsp_apicid(), IA32_APERF_MSR, 0) is None:
        # Writing of IA32_APERF MSR caused a GPF
        return None

    # Needs to busywait, not sleep
    start = time.time()
    while (time.time() - start < 1):
        pass

    mperf_hz = rdmsr(bsp_apicid(), IA32_MPERF_MSR)
    aperf_hz = rdmsr(bsp_apicid(), IA32_APERF_MSR)

    return mperf_hz, aperf_hz

def print_hz(hz):
    temp = hz / (1000.0 * 1000 * 1000)
    if abs(temp) >= 1:
        return '{:.03f} GHz'.format(temp)
    temp = hz / (1000.0 * 1000)
    if abs(temp) >= 1:
        return '{:.03f} MHz'.format(temp)
    temp = hz / 1000.0
    if abs(temp) >= 1:
        return '{:.03f} kHz'.format(temp)
    return '{} Hz'.format(hz)

def print_cpu_freq():
    frequency_data = cpu_frequency()
    if frequency_data == None:
        print "Frequency not measured since APERF and MPERF MSRs are not available."
        return
    mperf_hz, aperf_hz = cpu_frequency()
    delta_hz = aperf_hz - mperf_hz

    print "Frequency = {} (MPERF)  {} (APERF)  {} (delta)".format(print_hz(mperf_hz), print_hz(aperf_hz), print_hz(delta_hz))

def grouper(n, iterable, fillvalue=None):
    "grouper(3, 'ABCDEFG', 'x') --> ABC DEF Gxx"
    args = [iter(iterable)] * n
    return itertools.izip_longest(fillvalue=fillvalue, *args)

def dumpmem(mem, addr=0):
    """Dump hexadecimal and printable ASCII bytes for a memory buffer"""
    s = ''
    for offset, chunk in zip( range(0, len(mem), 16), grouper(16, mem) ):
        s += "{:08x}: ".format(addr + offset)
        s += " ".join("  " if x is None else "{:02x}".format(ord(x)) for x in chunk)
        s += "  "
        for x in chunk:
            if x is None:
                s += ' '
            elif x in string.letters or x in string.digits or x in string.punctuation:
                s += x
            else:
                s += '.'
        s += '\n'
    return s
