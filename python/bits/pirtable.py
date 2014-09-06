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

"""PCI Interrupt Routing Table module."""

import bits
import struct
import unpack
import testsuite
import ttypager

valid_address_ranges = [
    (0xF0000, 0x10000),
]

bad_address_ranges = [
    (0xE0000, 0x10000),
]

def find_pir_table():
    """Find and validate the address of the PCI Interrupt Routing table"""
    signature = struct.unpack("<I", "$PIR")[0]
    address_ranges = valid_address_ranges + bad_address_ranges
    for address, size in address_ranges:
        mem = bits.memory(address, size)
        for offset in range(0, len(mem), 16):
            if struct.unpack_from("<I", mem, offset)[0] == signature:
                table_size = struct.unpack_from("<H", mem, offset + 6)[0]
                if table_size <= (size - offset) and ((table_size - 32) % 16 == 0):
                    csum = sum(ord(mem[c]) for c in range(offset, offset + table_size))
                    if csum & 0xff == 0:
                        return bits.memory_addr(mem) + offset
    return None

class PIRTable(unpack.Struct):
    """Find and decode the PCI Interrupt Routing Table."""

    def __new__(cls):
        offset = find_pir_table()
        if offset is None:
            return None

        pir = super(PIRTable, cls).__new__(cls)
        pir._header_memory = bits.memory(offset, 0x20)
        return pir

    def __init__(self):
        super(PIRTable, self).__init__()
        u = unpack.Unpackable(self._header_memory)
        self.add_field('header', Header(u), "\n\n{!r}")

        self._table_memory = bits.memory(bits.memory_addr(self._header_memory), self.header.table_size)
        u = unpack.Unpackable(self._table_memory)
        u.skip(0x20)
        self.add_field('structures', unpack.unpack_all(u, [SlotEntry]), unpack.format_each("\n\n{!r}"))

class Header(unpack.Struct):
    def __init__(self, u):
        super(Header, self).__init__()
        self.raw_data = u.unpack_peek_rest()
        self.add_field('signature', u.unpack_one("4s"))
        self.add_field('version', u.unpack_one("<H"))
        self.add_field('table_size', u.unpack_one("<H"))
        self.add_field('pci_interrupt_router_bus', u.unpack_one("B"))
        self.add_field('pci_interrupt_router_dev_func', u.unpack_one("B"))
        self.add_field('pci_exclusive_irq_bitmap', u.unpack_one("<H"))
        self.add_field('compatible_pci_interrupt_router_vendor_id', u.unpack_one("<H"))
        self.add_field('compatible_pci_interrupt_router_device_id', u.unpack_one("<H"))
        self.add_field('miniport_data', u.unpack_one("<I"))
        u.skip(11)   # reserved byte
        self.add_field('checksum', u.unpack_one("B"))

class SlotEntry(unpack.Struct):
    def __init__(self, u):
        super(SlotEntry, self).__init__()
        self.start_offset = u.offset
        length = 16
        self.u = u.unpack_unpackable(length)
        self.raw_data = self.u.unpack_peek_rest()
        self.add_field('pci_bus_num', self.u.unpack_one("B"))
        self.add_field('pci_device_num', self.u.unpack_one("B"))
        self.add_field('link_value_INTA', self.u.unpack_one("B"))
        self.add_field('irq_bitmap_INTA', self.u.unpack_one("<H"))
        self.add_field('link_value_INTB', self.u.unpack_one("B"))
        self.add_field('irq_bitmap_INTB', self.u.unpack_one("<H"))
        self.add_field('link_value_INTC', self.u.unpack_one("B"))
        self.add_field('irq_bitmap_INTC', self.u.unpack_one("<H"))
        self.add_field('link_value_INTD', self.u.unpack_one("B"))
        self.add_field('irq_bitmap_INTD', self.u.unpack_one("<H"))
        self.add_field('slot_num', self.u.unpack_one("B"))
        self.u.skip(1)   # reserved byte
        del self.u

def dump_raw():
    try:
        pir = PIRTable()
        s = "PCI Interrupt Routing (PIR) Table -- Raw bytes and structure decode.\n\n"
        if pir:
            s += str(pir.header) + '\n'
            s += bits.dumpmem(pir._header_memory) + '\n'

            for slot_entry in pir.structures:
                s += str(slot_entry) + '\n'
                s += bits.dumpmem(slot_entry.raw_data) + '\n'
        else:
            s += "PCI Interrupt Routing (PIR) Table not found.\n"
        ttypager.ttypager_wrap(s, indent=False)
    except:
        print "Error parsing PCI Interrupt Routing Table information:"
        import traceback
        traceback.print_exc()

def dump():
    try:
        pir = PIRTable()
        s = "PCI Interrupt Routing (PIR) Table -- Structure decode.\n\n"
        if pir:
            s += str(pir)
        else:
            s += "PCI Interrupt Routing (PIR) Table not found.\n"
        ttypager.ttypager_wrap(s, indent=False)
    except:
        print "Error parsing PCI Interrupt Routing (PIR) Table information:"
        import traceback
        traceback.print_exc()

def register_tests():
    testsuite.add_test("PCI Interrupt Routing Table", test_pirtable)

def test_pirtable():
    """Test the PCI Interrupt Routing Table"""
    pir = PIRTable()
    if pir is None:
        return
    addr = bits.memory_addr(pir._table_memory)
    for address, size in bad_address_ranges:
        if addr >= address and addr < address + size:
            bad_address = True
            break
    else:
        bad_address = False
    testsuite.test('PCI Interrupt Routing Table spec-compliant address', not bad_address)
    testsuite.print_detail('Found PCI Interrupt Routing Table at bad address {:#x}'.format(addr))
    testsuite.print_detail('$PIR Structure must appear at a 16-byte-aligned address')
    testsuite.print_detail('located in the 0xF0000 to 0xFFFFF block')
