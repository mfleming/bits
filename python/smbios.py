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

"""SMBIOS/DMI module."""

import bits
import bitfields
import redirect
import struct
import uuid
import unpack
import ttypager
import sys

class SMBIOS(unpack.Struct):
    def __new__(cls):
        if sys.platform == "BITS-EFI":
            import efi
            sm_ptr = efi.system_table.ConfigurationTableDict.get(efi.SMBIOS_TABLE_GUID)
        else:
            mem = bits.memory(0xF0000, 0x10000)
            signature = struct.unpack("<I", "_SM_")[0]
            for offset in range(0, len(mem), 16):
                if struct.unpack_from("I", mem, offset)[0] == signature:
                    entry_point_length = struct.unpack_from("B", mem, offset + 5)[0]
                    csum = sum(map(ord, mem[offset:offset + entry_point_length])) & 0xff
                    if csum == 0:
                        sm_ptr = bits.memory_addr(mem) + offset
                        break
            else:
                return None

        if not sm_ptr:
            return None

        sm = super(SMBIOS, cls).__new__(cls)
        sm._header_memory = bits.memory(sm_ptr, 0x1f)
        return sm

    def __init__(self):
        super(SMBIOS, self).__init__()
        u = unpack.Unpackable(self._header_memory)
        self.add_field('header', Header(u))
        self._structure_memory = bits.memory(self.header.structure_table_address, self.header.structure_table_length)
        u = unpack.Unpackable(self._structure_memory)
        self.add_field('structures', unpack.unpack_all(u, _smbios_structures, self), unpack.format_each("\n\n{!r}"))

class Header(unpack.Struct):
    def __new__(cls, u):
        return super(Header, cls).__new__(cls)

    def __init__(self, u):
        super(Header, self).__init__()
        self.raw_data = u.unpack_rest()
        u = unpack.Unpackable(self.raw_data)
        self.add_field('anchor_string', u.unpack_one("4s"))
        self.add_field('checksum', u.unpack_one("B"))
        self.add_field('length', u.unpack_one("B"))
        self.add_field('major_version', u.unpack_one("B"))
        self.add_field('minor_version', u.unpack_one("B"))
        self.add_field('max_structure_size', u.unpack_one("<H"))
        self.add_field('entry_point_revision', u.unpack_one("B"))
        self.add_field('formatted_area', u.unpack_one("5s"))
        self.add_field('intermediate_anchor_string', u.unpack_one("5s"))
        self.add_field('intermediate_checksum', u.unpack_one("B"))
        self.add_field('structure_table_length', u.unpack_one("<H"))
        self.add_field('structure_table_address', u.unpack_one("<I"))
        self.add_field('number_structures', u.unpack_one("<H"))
        self.add_field('bcd_revision', u.unpack_one("B"))
        if not u.at_end():
            self.add_field('data', u.unpack_rest())

class SmbiosBaseStructure(unpack.Struct):
    def __new__(cls, u, sm):
        t = u.unpack_peek_one("B")
        if cls.smbios_structure_type is not None and t != cls.smbios_structure_type:
            return None
        return super(SmbiosBaseStructure, cls).__new__(cls)

    def __init__(self, u, sm):
        super(SmbiosBaseStructure, self).__init__()
        self.start_offset = u.offset
        length = u.unpack_peek_one("<xB")
        self.raw_data = u.unpack_raw(length)
        self.u = unpack.Unpackable(self.raw_data)

        self.strings_offset = u.offset
        def unpack_string():
            return "".join(iter(lambda: u.unpack_one("c"), "\x00"))
        strings = list(iter(unpack_string, ""))
        if not strings:
            u.skip(1)

        self.strings_length = u.offset - self.strings_offset
        self.raw_strings = str(bits.memory(sm.header.structure_table_address + self.strings_offset, self.strings_length))

        if len(strings):
            self.strings = strings

        self.add_field('type', self.u.unpack_one("B"))
        self.add_field('length', self.u.unpack_one("B"))
        self.add_field('handle', self.u.unpack_one("<H"))

    def fini(self):
        if not self.u.at_end():
            self.add_field('data', self.u.unpack_rest())
        del self.u

    def fmtstr(self, i):
        """Format the specified index and the associated string"""
        return "{} '{}'".format(i, self.getstr(i))

    def getstr(self, i):
        """Get the string associated with the given index"""
        if i == 0:
            return "(none)"
        if not hasattr(self, "strings"):
            return "(error: structure has no strings)"
        if i > len(self.strings):
            return "(error: string index out of range)"
        return self.strings[i - 1]

class BIOSInformation(SmbiosBaseStructure):
    smbios_structure_type = 0

    def __init__(self, u, sm):
        super(BIOSInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('vendor', u.unpack_one("B"), self.fmtstr)
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('starting_address_segment', u.unpack_one("<H"))
            self.add_field('release_date', u.unpack_one("B"), self.fmtstr)
            self.add_field('rom_size', u.unpack_one("B"))
            self.add_field('characteristics', u.unpack_one("<Q"))
            minor_version_str = str(sm.header.minor_version) # 34 is .34, 4 is .4, 41 is .41; compare ASCIIbetically to compare initial digits rather than numeric value
            if (sm.header.major_version, minor_version_str) >= (2,"4"):
                characteristic_bytes = 2
            else:
                characteristic_bytes = self.length - 0x12
            self.add_field('characteristics_extensions', [u.unpack_one("B") for b in range(characteristic_bytes)])
            if (sm.header.major_version, minor_version_str) >= (2,"4"):
                self.add_field('major_release', u.unpack_one("B"))
                self.add_field('minor_release', u.unpack_one("B"))
                self.add_field('ec_major_release', u.unpack_one("B"))
                self.add_field('ec_minor_release', u.unpack_one("B"))
        except:
            self.decode_failure = True
            print "Error parsing BIOSInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemInformation(SmbiosBaseStructure):
    smbios_structure_type = 1

    def __init__(self, u, sm):
        super(SystemInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('product_name', u.unpack_one("B"), self.fmtstr)
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x8:
                self.add_field('uuid', uuid.UUID(bytes_le=u.unpack_one("16s")))
                wakeup_types = {
                    0: 'Reserved',
                    1: 'Other',
                    2: 'Unknown',
                    3: 'APM Timer',
                    4: 'Modem Ring',
                    5: 'LAN Remote',
                    6: 'Power Switch',
                    7: 'PCI PME#',
                    8: 'AC Power Restored'
                }
                self.add_field('wakeup_type', u.unpack_one("B"), unpack.format_table("{}", wakeup_types))
            if self.length > 0x19:
                self.add_field('sku_number', u.unpack_one("B"), self.fmtstr)
                self.add_field('family', u.unpack_one("B"), self.fmtstr)
        except:
            self.decode_failure = True
            print "Error parsing SystemInformation"
            import traceback
            traceback.print_exc()
        self.fini()

_board_types = {
    1: 'Unknown',
    2: 'Other',
    3: 'Server Blade',
    4: 'Connectivity Switch',
    5: 'System Management Module',
    6: 'Processor Module',
    7: 'I/O Module',
    8: 'Memory Module',
    9: 'Daughter Board',
    0xA: 'Motherboard',
    0xB: 'Processor/Memory Module',
    0xC: 'Processor/IO Module',
    0xD: 'Interconnect Board'
}

class BaseboardInformation(SmbiosBaseStructure):
    smbios_structure_type = 2

    def __init__(self, u, sm):
        super(BaseboardInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('product', u.unpack_one("B"), self.fmtstr)
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)

            if self.length > 0x8:
                self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)

            if self.length > 0x9:
                self.add_field('feature_flags', u.unpack_one("B"))
                self.add_field('hosting_board', bool(bitfields.getbits(self.feature_flags, 0)), "feature_flags[0]={}")
                self.add_field('requires_daughter_card', bool(bitfields.getbits(self.feature_flags, 1)), "feature_flags[1]={}")
                self.add_field('removable', bool(bitfields.getbits(self.feature_flags, 2)), "feature_flags[2]={}")
                self.add_field('replaceable', bool(bitfields.getbits(self.feature_flags, 3)), "feature_flags[3]={}")
                self.add_field('hot_swappable', bool(bitfields.getbits(self.feature_flags, 4)), "feature_flags[4]={}")

            if self.length > 0xA:
                self.add_field('location', u.unpack_one("B"), self.fmtstr)

            if self.length > 0xB:
                self.add_field('chassis_handle', u.unpack_one("<H"))

            if self.length > 0xD:
                self.add_field('board_type', u.unpack_one("B"), unpack.format_table("{}", _board_types))

            if self.length > 0xE:
                self.add_field('handle_count', u.unpack_one("B"))
                if self.handle_count > 0:
                    self.add_field('contained_object_handles', tuple(u.unpack_one("<H") for i in range(self.handle_count)))
        except:
            self.decode_failure = True
            print "Error parsing BaseboardInformation"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemEnclosure(SmbiosBaseStructure):
    smbios_structure_type = 3

    def __init__(self, u, sm):
        super(SystemEnclosure, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('enumerated_type', u.unpack_one("B"))
            self.add_field('chassis_lock_present', bool(bitfields.getbits(self.enumerated_type, 7)), "type[7]={}")
            board_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Desktop',
                0x04: 'Low Profile Desktop',
                0x05: 'Pizza Box',
                0x06: 'Mini Tower',
                0x07: 'Tower',
                0x08: 'Portable',
                0x09: 'Laptop',
                0x0A: 'Notebook',
                0x0B: 'Hand Held',
                0x0C: 'Docking Station',
                0x0D: 'All in One',
                0x0E: 'Sub Notebook',
                0x0F: 'Space-saving',
                0x10: 'Lunch Box',
                0x11: 'Main Server Chassis',
                0x12: 'Expansion Chassis',
                0x13: 'SubChassis',
                0x14: 'Bus Expansion Chassis',
                0x15: 'Peripheral Chassis',
                0x16: 'RAID Chassis',
                0x17: 'Rack Mount Chassis',
                0x18: 'Sealed-case PC',
                0x19: 'Multi-system chassis W',
                0x1A: 'Compact PCI',
                0x1B: 'Advanced TCA',
                0x1C: 'Blade',
                0x1D: 'Blade Enclosure',
            }
            self.add_field('system_enclosure_type', bitfields.getbits(self.enumerated_type, 6, 0), unpack.format_table("enumerated_type[6:0]={}", board_types))
            self.add_field('version', u.unpack_one("B"), self.fmtstr)
            self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
            self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)
            minor_version_str = str(sm.header.minor_version) # 34 is .34, 4 is .4, 41 is .41; compare ASCIIbetically to compare initial digits rather than numeric value
            if self.length > 9:
                chassis_states = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'Safe',
                    0x04: 'Warning',
                    0x05: 'Critical',
                    0x06: 'Non-recoverable',
                }
                self.add_field('bootup_state', u.unpack_one("B"), unpack.format_table("{}", chassis_states))
                self.add_field('power_supply_state', u.unpack_one("B"), unpack.format_table("{}", chassis_states))
                self.add_field('thermal_state', u.unpack_one("B"), unpack.format_table("{}", chassis_states))
                security_states = {
                    0x01: 'Other',
                    0x02: 'Unknown',
                    0x03: 'None',
                    0x04: 'External interface locked out',
                    0x05: 'External interface enabled',
                }
                self.add_field('security_status', u.unpack_one("B"), unpack.format_table("{}", security_states))
            if self.length > 0xd:
                self.add_field('oem_defined', u.unpack_one("<I"))
            if self.length > 0x11:
                self.add_field('height', u.unpack_one("B"))
                self.add_field('num_power_cords', u.unpack_one("B"))
                self.add_field('contained_element_count', u.unpack_one("B"))
                self.add_field('contained_element_length', u.unpack_one("B"))
            if getattr(self, 'contained_element_count', 0):
                self.add_field('contained_elements', tuple(SystemEnclosureContainedElement(u, self.contained_element_length) for i in range(self.contained_element_count)))
            if self.length > (0x15 + (getattr(self, 'contained_element_count', 0) * getattr(self, 'contained_element_length', 0))):
                self.add_field('sku_number', u.unpack_one("B"), self.fmtstr)
        except:
            self.decode_failure = True
            print "Error parsing SystemEnclosure"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemEnclosureContainedElement(unpack.Struct):
    def __init__(self, u, length):
        super(SystemEnclosureContainedElement, self).__init__()
        self.start_offset = u.offset
        self.raw_data = u.unpack_raw(length)
        self.u = unpack.Unpackable(self.raw_data)
        u = self.u
        self.add_field('contained_element_type', u.unpack_one("B"))
        type_selections = {
            0: 'SMBIOS baseboard type enumeration',
            1: 'SMBIOS structure type enumeration',
        }
        self.add_field('type_select', bitfields.getbits(self.type, 7), unpack.format_table("contained_element_type[7]={}", type_selections))
        self.add_field('type', bitfields.getbits(self.type, 6, 0))
        if self.type_select == 0:
            self.add_field('smbios_board_type', self.type, unpack.format_table("{}", _board_types))
        else:
            self.add_field('smbios_structure_type', self.type)
        self.add_field('minimum', u.unpack_one("B"))
        self.add_field('maximum', u.unpack_one("B"))
        if not u.at_end():
            self.add_field('data', u.unpack_rest())
        del self.u

class ProcessorInformation(SmbiosBaseStructure):
    smbios_structure_type = 4

    def __init__(self, u, sm):
        super(ProcessorInformation, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('socket_designation', u.unpack_one("B"), self.fmtstr)
            processor_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Central Processor',
                0x04: 'Math Processor',
                0x05: 'DSP Processor',
                0x06: 'Video Processor',
            }
            self.add_field('processor_type', u.unpack_one("B"), unpack.format_table("{}", processor_types))
            self.add_field('processor_family', u.unpack_one("B"))
            self.add_field('processor_manufacturer', u.unpack_one("B"), self.fmtstr)
            self.add_field('processor_id', u.unpack_one("<Q"))
            self.add_field('processor_version', u.unpack_one("B"), self.fmtstr)
            self.add_field('voltage', u.unpack_one("B"))
            self.add_field('external_clock', u.unpack_one("<H"))
            self.add_field('max_speed', u.unpack_one("<H"))
            self.add_field('current_speed', u.unpack_one("<H"))
            self.add_field('status', u.unpack_one("B"))
            processor_upgrades = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Daughter Board',
                0x04: 'ZIF Socket',
                0x05: 'Replaceable Piggy Back',
                0x06: 'None',
                0x07: 'LIF Socket',
                0x08: 'Slot 1',
                0x09: 'Slot 2',
                0x0A: '370-pin socket',
                0x0B: 'Slot A',
                0x0C: 'Slot M',
                0x0D: 'Socket 423',
                0x0E: 'Socket A (Socket 462)',
                0x0F: 'Socket 478',
                0x10: 'Socket 754',
                0x11: 'Socket 940',
                0x12: 'Socket 939',
                0x13: 'Socket mPGA604',
                0x14: 'Socket LGA771',
                0x15: 'Socket LGA775',
                0x16: 'Socket S1',
                0x17: 'Socket AM2',
                0x18: 'Socket F (1207)',
                0x19: 'Socket LGA1366',
                0x1A: 'Socket G34',
                0x1B: 'Socket AM3',
                0x1C: 'Socket C32',
                0x1D: 'Socket LGA1156',
                0x1E: 'Socket LGA1567',
                0x1F: 'Socket PGA988A',
                0x20: 'Socket BGA1288',
                0x21: 'Socket rPGA988B',
                0x22: 'Socket BGA1023',
                0x23: 'Socket BGA1224',
                0x24: 'Socket BGA1155',
                0x25: 'Socket LGA1356',
                0x26: 'Socket LGA2011',
                0x27: 'Socket FS1',
                0x28: 'Socket FS2',
                0x29: 'Socket FM1',
                0x2A: 'Socket FM2',
            }
            self.add_field('processor_upgrade', u.unpack_one("B"), unpack.format_table("{}", processor_upgrades))
            if self.length > 0x1A:
                self.add_field('l1_cache_handle', u.unpack_one("<H"))
                self.add_field('l2_cache_handle', u.unpack_one("<H"))
                self.add_field('l3_cache_handle', u.unpack_one("<H"))
            if self.length > 0x20:
                self.add_field('serial_number', u.unpack_one("B"), self.fmtstr)
                self.add_field('asset_tag', u.unpack_one("B"), self.fmtstr)
                self.add_field('part_number', u.unpack_one("B"), self.fmtstr)
            if self.length > 0x24:
                self.add_field('core_count', u.unpack_one("B"))
                self.add_field('core_enabled', u.unpack_one("B"))
                self.add_field('thread_count', u.unpack_one("B"))
                self.add_field('processor_characteristics', u.unpack_one("<H"))
            if self.length > 0x28:
                self.add_field('processor_family_2', u.unpack_one("<H"))
        except:
            self.decode_failure = True
            print "Error parsing Processor Information"
            import traceback
            traceback.print_exc()
        self.fini()

class PortConnectorInfo(SmbiosBaseStructure):
    smbios_structure_type = 8

    def __init__(self, u, sm):
        super(PortConnectorInfo, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('internal_reference_designator', u.unpack_one("B"), self.fmtstr)
            connector_types = {
                0x00: 'None',
                0x01: 'Centronics',
                0x02: 'Mini Centronics',
                0x03: 'Proprietary',
                0x04: 'DB-25 pin male',
                0x05: 'DB-25 pin female',
                0x06: 'DB-15 pin male',
                0x07: 'DB-15 pin female',
                0x08: 'DB-9 pin male',
                0x09: 'DB-9 pin female',
                0x0A: 'RJ-11',
                0x0B: 'RJ-45',
                0x0C: '50-pin MiniSCSI',
                0x0D: 'Mini-DIN',
                0x0E: 'Micro-DIN',
                0x0F: 'PS/2',
                0x10: 'Infrared',
                0x11: 'HP-HIL',
                0x12: 'Access Bus (USB)',
                0x13: 'SSA SCSI',
                0x14: 'Circular DIN-8 male',
                0x15: 'Circular DIN-8 female',
                0x16: 'On Board IDE',
                0x17: 'On Board Floppy',
                0x18: '9-pin Dual Inline (pin 10 cut)',
                0x19: '25-pin Dual Inline (pin 26 cut)',
                0x1A: '50-pin Dual Inline',
                0x1B: '68-pin Dual Inline',
                0x1C: 'On Board Sound Input from CD-ROM',
                0x1D: 'Mini-Centronics Type-14',
                0x1E: 'Mini-Centronics Type-26',
                0x1F: 'Mini-jack (headphones)',
                0x20: 'BNC',
                0x21: '1394',
                0x22: 'SAS/SATA Plug Receptacle',
                0xA0: 'PC-98',
                0xA1: 'PC-98Hireso',
                0xA2: 'PC-H98',
                0xA3: 'PC-98Note',
                0xA4: 'PC-98Full',
                0xFF: 'Other',
            }
            self.add_field('internal_connector_type', u.unpack_one("B"), unpack.format_table("{}", connector_types))
            self.add_field('external_reference_designator', u.unpack_one("B"), self.fmtstr)
            self.add_field('external_connector_type', u.unpack_one("B"), unpack.format_table("{}", connector_types))
            port_types = {
                0x00: 'None',
                0x01: 'Parallel Port XT/AT Compatible',
                0x02: 'Parallel Port PS/2',
                0x03: 'Parallel Port ECP',
                0x04: 'Parallel Port EPP',
                0x05: 'Parallel Port ECP/EPP',
                0x06: 'Serial Port XT/AT Compatible',
                0x07: 'Serial Port 16450 Compatible',
                0x08: 'Serial Port 16550 Compatible',
                0x09: 'Serial Port 16550A Compatible',
                0x0A: 'SCSI Port',
                0x0B: 'MIDI Port',
                0x0C: 'Joy Stick Port',
                0x0D: 'Keyboard Port',
                0x0E: 'Mouse Port',
                0x0F: 'SSA SCSI',
                0x10: 'USB',
                0x11: 'FireWire (IEEE P1394)',
                0x12: 'PCMCIA Type I2',
                0x13: 'PCMCIA Type II',
                0x14: 'PCMCIA Type III',
                0x15: 'Cardbus',
                0x16: 'Access Bus Port',
                0x17: 'SCSI II',
                0x18: 'SCSI Wide',
                0x19: 'PC-98',
                0x1A: 'PC-98-Hireso',
                0x1B: 'PC-H98',
                0x1C: 'Video Port',
                0x1D: 'Audio Port',
                0x1E: 'Modem Port',
                0x1F: 'Network Port',
                0x20: 'SATA',
                0x21: 'SAS',
                0xA0: '8251 Compatible',
                0xA1: '8251 FIFO Compatible',
                0xFF: 'Other',
            }
            self.add_field('port_type', u.unpack_one("B"), unpack.format_table("{}", port_types))
        except:
            self.decodeFailure = True
            print "Error parsing PortConnectorInfo"
            import traceback
            traceback.print_exc()
        self.fini()

class SystemSlots(SmbiosBaseStructure):
    smbios_structure_type = 9

    def __init__(self, u, sm):
        super(SystemSlots, self).__init__(u, sm)
        u = self.u
        try:
            self.add_field('designation', u.unpack_one("B"), self.fmtstr)
            _slot_types = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'ISA',
                0x04: 'MCA',
                0x05: 'EISA',
                0x06: 'PCI',
                0x07: 'PC Card (PCMCIA)',
                0x08: 'VL-VESA',
                0x09: 'Proprietary',
                0x0A: 'Processor Card Slot',
                0x0B: 'Proprietary Memory Card Slot',
                0x0C: 'I/O Riser Card Slot',
                0x0D: 'NuBus',
                0x0E: 'PCI 66MHz Capable',
                0x0F: 'AGP',
                0x10: 'AGP 2X',
                0x11: 'AGP 4X',
                0x12: 'PCI-X',
                0x13: 'AGP 8X',
                0xA0: 'PC-98/C20',
                0xA1: 'PC-98/C24',
                0xA2: 'PC-98/E',
                0xA3: 'PC-98/Local Bus',
                0xA4: 'PC-98/Card',
                0xA5: 'PCI Express',
                0xA6: 'PCI Express x1',
                0xA7: 'PCI Express x2',
                0xA8: 'PCI Express x4',
                0xA9: 'PCI Express x8',
                0xAA: 'PCI Express x16',
                0xAB: 'PCI Express Gen 2',
                0xAC: 'PCI Express Gen 2 x1',
                0xAD: 'PCI Express Gen 2 x2',
                0xAE: 'PCI Express Gen 2 x4',
                0xAF: 'PCI Express Gen 2 x8',
                0xB0: 'PCI Express Gen 2 x16',
                0xB1: 'PCI Express Gen 3',
                0xB2: 'PCI Express Gen 3 x1',
                0xB3: 'PCI Express Gen 3 x2',
                0xB4: 'PCI Express Gen 3 x4',
                0xB5: 'PCI Express Gen 3 x8',
                0xB6: 'PCI Express Gen 3 x16',
            }
            self.add_field('slot_type', u.unpack_one("B"), unpack.format_table("{}", _slot_types))
            _slot_data_bus_widths = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: '8 bit',
                0x04: '16 bit',
                0x05: '32 bit',
                0x06: '64 bit',
                0x07: '128 bit',
                0x08: '1x or x1',
                0x09: '2x or x2',
                0x0A: '4x or x4',
                0x0B: '8x or x8',
                0x0C: '12x or x12',
                0x0D: '16x or x16',
                0x0E: '32x or x32',
            }
            self.add_field('slot_data_bus_width', u.unpack_one('B'), unpack.format_table("{}", _slot_data_bus_widths))
            _current_usages = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Available',
                0x04: 'In use',
            }
            self.add_field('current_usage', u.unpack_one('B'), unpack.format_table("{}", _current_usages))
            _slot_lengths = {
                0x01: 'Other',
                0x02: 'Unknown',
                0x03: 'Short Length',
                0x04: 'Long Length',
            }
            self.add_field('slot_length', u.unpack_one('B'), unpack.format_table("{}", _slot_lengths))
            self.add_field('slot_id', u.unpack_one('<H'))
            self.add_field('characteristics1', u.unpack_one('B'))
            self.add_field('characteristics_unknown', bool(bitfields.getbits(self.characteristics1, 0)), "characteristics1[0]={}")
            self.add_field('provides_5_0_volts', bool(bitfields.getbits(self.characteristics1, 1)), "characteristics1[1]={}")
            self.add_field('provides_3_3_volts', bool(bitfields.getbits(self.characteristics1, 2)), "characteristics1[2]={}")
            self.add_field('shared_slot', bool(bitfields.getbits(self.characteristics1, 3)), "characteristics1[3]={}")
            self.add_field('supports_pc_card_16', bool(bitfields.getbits(self.characteristics1, 4)), "characteristics1[4]={}")
            self.add_field('supports_cardbus', bool(bitfields.getbits(self.characteristics1, 5)), "characteristics1[5]={}")
            self.add_field('supports_zoom_video', bool(bitfields.getbits(self.characteristics1, 6)), "characteristics1[6]={}")
            self.add_field('supports_modem_ring_resume', bool(bitfields.getbits(self.characteristics1, 7)), "characteristics1[7]={}")
            if self.length > 0x0C:
                self.add_field('characteristics2', u.unpack_one('B'))
                self.add_field('supports_PME', bool(bitfields.getbits(self.characteristics2, 0)), "characteristics2[0]={}")
                self.add_field('supports_hot_plug', bool(bitfields.getbits(self.characteristics2, 1)), "characteristics2[1]={}")
                self.add_field('supports_smbus', bool(bitfields.getbits(self.characteristics2, 2)), "characteristics2[2]={}")
            if self.length > 0x0D:
                self.add_field('segment_group_number', u.unpack_one('<H'))
                self.add_field('bus_number', u.unpack_one('B'))
                self.add_field('device_function_number', u.unpack_one('B'))
                self.add_field('device_number', bitfields.getbits(self.device_function_number, 7, 3), "device_function_number[7:3]={}")
                self.add_field('function_number', bitfields.getbits(self.device_function_number, 2, 0), "device_function_number[2:0]={}")
        except:
            self.decodeFailure = True
            print "Error parsing SystemSlots"
            import traceback
            traceback.print_exc()
        self.fini()

class Inactive(SmbiosBaseStructure):
    smbios_structure_type = 126

    def __init__(self, u, sm):
        super(Inactive, self).__init__(u, sm)
        self.fini()

class EndOfTable(SmbiosBaseStructure):
    smbios_structure_type = 127

    def __init__(self, u, sm):
        super(EndOfTable, self).__init__(u, sm)
        self.fini()

class SmbiosStructureUnknown(SmbiosBaseStructure):
    smbios_structure_type = None

    def __init__(self, u, sm):
        super(SmbiosStructureUnknown, self).__init__(u, sm)
        self.fini()

_smbios_structures = [
    BIOSInformation,
    SystemInformation,
    BaseboardInformation,
    SystemEnclosure,
    ProcessorInformation,
    PortConnectorInfo,
    SystemSlots,
    EndOfTable,
    SmbiosStructureUnknown, # Must always come last
]

def log_smbios_info():
    with redirect.logonly():
        try:
            sm = SMBIOS()
            print
            if sm is None:
                print "No SMBIOS structures found"
                return
            output = {}
            known_types = (0, 1)
            for sm_struct in sm.structures:
                if sm_struct.type in known_types:
                    output.setdefault(sm_struct.type, []).append(sm_struct)
                    if len(output) == len(known_types):
                        break

            print "SMBIOS information:"
            for key in sorted(known_types):
                for s in output.get(key, ["No structure of type {} found".format(key)]):
                    print ttypager._wrap("{}: {}".format(key, s))
        except:
            print "Error parsing SMBIOS information:"
            import traceback
            traceback.print_exc()

def dump_raw():
    try:
        sm = SMBIOS()
        if sm:
            s = "SMBIOS -- Raw bytes and structure decode.\n\n"

            s += str(sm.header) + '\n'
            s += bits.dumpmem(sm._header_memory) + '\n'

            s += "Raw bytes for the SMBIOS structures\n"
            s += bits.dumpmem(sm._structure_memory) + '\n'

            for sm_struct in sm.structures:
                s += str(sm_struct) + '\n'
                s += bits.dumpmem(sm_struct.raw_data)

                s += "Strings:\n"
                for n in range(1, len(getattr(sm_struct, "strings", [])) + 1):
                    s += str(sm_struct.fmtstr(n)) + '\n'
                s += bits.dumpmem(sm_struct.raw_strings) + '\n'
        else:
            s = "No SMBIOS structures found"
        ttypager.ttypager_wrap(s, indent=False)
    except:
        print "Error parsing SMBIOS information:"
        import traceback
        traceback.print_exc()

def dump():
    try:
        sm = SMBIOS()
        if sm:
            s = str(sm)
        else:
            s = "No SMBIOS structures found"
        ttypager.ttypager_wrap(s, indent=False)
    except:
        print "Error parsing SMBIOS information:"
        import traceback
        traceback.print_exc()
