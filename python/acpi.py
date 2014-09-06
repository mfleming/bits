# Copyright (c) 2014, Intel Corporation
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

"""ACPI module."""

import _acpi
import bits
import bits.pyfs
import bitfields
from cpudetect import cpulib
from collections import OrderedDict
import itertools
import os
import struct
import ttypager
import unpack
import uuid

from _acpi import _get_table_by_index as get_table_by_index
from _acpi import _install_interface as install_interface
from _acpi import _objpaths as get_objpaths
from _acpi import _remove_interface as remove_interface
from _acpi import _terminate as terminate

def _id(v):
    return v

class TableParseException(Exception): pass

class AcpiBuffer(str):
    def __repr__(self):
        return "AcpiBuffer(" + ' '.join("{:02x}".format(ord(c)) for c in self) + ")"

    def __str__(self):
        return repr(self)

def display_resources(name):
    with ttypager.page():
        for r in get_objpaths(name):
            raw_descriptor = evaluate(r)
            print r
            print repr(raw_descriptor)
            print repr(ResourceDescriptors(raw_descriptor)) + "\n"

class ResourceDescriptors(unpack.Struct):
    """Decoder for resource descriptors.

    _CRS, _PRS, and _SRS control methods use packages of resource descriptors to
    describe the resource requirements of devices."""

    def __init__(self, data):
        super(ResourceDescriptors, self).__init__()
        u = unpack.Unpackable(data)
        resources = unpack.unpack_all(u, _resource_descriptors)
        self.add_field('resources', resources, unpack.format_each("\n{!r}"))

SMALL_RESOURCE, LARGE_RESOURCE = 0, 1

class BaseResourceDescriptor(unpack.Struct):
    def __new__(cls, u):
        tag = u.unpack_peek_one("B")
        rtype = bitfields.getbits(tag, 7)
        if rtype == LARGE_RESOURCE:
            iname = bitfields.getbits(tag, 6, 0)
        else:
            iname = bitfields.getbits(tag, 6, 3)
        if (cls.resource_type is not None) and (rtype != cls.resource_type or iname != cls.item_name):
            return None
        return super(BaseResourceDescriptor, cls).__new__(cls)

    def __init__(self, u):
        super(BaseResourceDescriptor, self).__init__()
        tag = u.unpack_peek_one("B")
        rtype = bitfields.getbits(tag, 7)
        if rtype == LARGE_RESOURCE:
            # Large resource data type
            length = u.unpack_peek_one("<xH") + 3
        else:
            # Small resource data type
            length = bitfields.getbits(tag, 2, 0) + 1
        self.u = u.unpack_unpackable(length)
        self.add_field('type', self.u.unpack_one("B"))
        if rtype == LARGE_RESOURCE:
            self.add_field('length', self.u.unpack_one("<H"))

    def fini(self):
        if not self.u.at_end():
            self.add_field('data', self.u.unpack_rest())
        del self.u

class SmallResourceDescriptor(BaseResourceDescriptor):
    resource_type = SMALL_RESOURCE

class LargeResourceDescriptor(BaseResourceDescriptor):
    resource_type = LARGE_RESOURCE

class IRQDescriptor(SmallResourceDescriptor):
    item_name = 0x04

    def __init__(self, u):
        super(IRQDescriptor, self).__init__(u)
        self.add_field('_INT', self.u.unpack_one("<H"))
        if not self.u.at_end():
            self.add_field('information', self.u.unpack_one("B"))
            interrupt_sharing_wakes = {
                0x0: "Exclusive",
                0x1: "Shared",
                0x2: "ExclusiveAndWake",
                0x3: "SharedAndWake",
            }
            self.add_field('_SHR', bitfields.getbits(self.information, 5, 4), unpack.format_table("{}", interrupt_sharing_wakes))
            interrupt_polarities = {
               0: "Active-High",
               1: "Active-Low",
            }
            self.add_field('_LL', bitfields.getbits(self.information, 3), unpack.format_table("{}", interrupt_polarities))
            interrupt_modes = {
               0: "Level-Triggered",
               1: "Edge-Triggered",
            }
            self.add_field('_HE', bitfields.getbits(self.information, 0), unpack.format_table("{}", interrupt_modes))
        self.fini()

class DMADescriptor(SmallResourceDescriptor):
    item_name = 0x05

    def __init__(self, u):
        super(DMADescriptor, self).__init__(u)
        self.add_field('_DMA', self.u.unpack_one("B"))
        self.add_field('mask', self.u.unpack_one("B"))
        dma_types = {
            0b00: "compatibility mode",
            0b01: "Type A",
            0b10: "Type B",
            0b11: "Type F",
        }
        self.add_field('_TYP', bitfields.getbits(self.mask, 6, 5), unpack.format_table("mask[6:5]={}", dma_types))
        self.add_field('_BM', bool(bitfields.getbits(self.mask, 2)), "mask[2]={}")
        transfer_type_preferences = {
            0b00: "8-bit only",
            0b01: "8- and 16-bit",
            0b10: "16-bit only",
        }
        self.add_field('_SIZ', bitfields.getbits(self.mask, 1, 0), unpack.format_table("mask[1:0]={}", transfer_type_preferences))
        self.fini()

class StartDependentFunctionsDescriptor(SmallResourceDescriptor):
    item_name = 0x06

    def __init__(self, u):
        super(StartDependentFunctionsDescriptor, self).__init__(u)
        if not self.u.at_end():
            self.add_field('priority', self.u.unpack_one("B"))
            configurations = {
                0: "Good configuration",
                1: "Acceptable configuration",
                2: "Sub-optimal configuration",
            }
            self.add_field('compatibility_priority', bitfields.getbits(self.priority, 1, 0), unpack.format_table("priority[1:0]={}", configurations))
            self.add_field('performance_robustness', bitfields.getbits(self.priority, 3, 2), unpack.format_table("priority[3:2]={}", configurations))
        self.fini()

class EndDependentFunctionsDescriptor(SmallResourceDescriptor):
    item_name = 0x07

    def __init__(self, u):
        super(EndDependentFunctionsDescriptor, self).__init__(u)
        self.fini()

class IOPortDescriptor(SmallResourceDescriptor):
    item_name = 0x08

    def __init__(self, u):
        super(IOPortDescriptor, self).__init__(u)
        self.add_field('information', self.u.unpack_one("B"))
        self.add_field('_DEC', bitfields.getbits(self.information, 0))
        self.add_field('_MIN', self.u.unpack_one("<H"))
        self.add_field('_MAX', self.u.unpack_one("<H"))
        self.add_field('_ALN', self.u.unpack_one("B"))
        self.add_field('_LEN', self.u.unpack_one("B"))
        self.fini()

class FixedIOPortDescriptor(SmallResourceDescriptor):
    item_name = 0x09

    def __init__(self, u):
        super(FixedIOPortDescriptor, self).__init__(u)
        self.add_field('_BAS', self.u.unpack_one("<H"))
        self.add_field('_LEN', self.u.unpack_one("B"))
        self.fini()

class FixedDMADescriptor(SmallResourceDescriptor):
    item_name = 0x0A

    def __init__(self, u):
        super(FixedDMADescriptor, self).__init__(u)
        self.add_field('_DMA', self.u.unpack_one("<H"))
        self.add_field('_TYPE', self.u.unpack_one("<H"))
        _dma_transfer_widths = {
            0x00: "8-bit",
            0x01: "16-bit",
            0x02: "32-bit",
            0x03: "64-bit",
            0x04: "128-bit",
            0x05: "256-bit",
        }
        self.add_field('_SIZ', self.u.unpack_one("B"), unpack.format_table("DMA transfer width={}", _dma_transfer_widths))
        self.fini()

class VendorDefinedSmallDescriptor(SmallResourceDescriptor):
    item_name = 0x0E

    def __init__(self, u):
        super(VendorDefinedSmallDescriptor, self).__init__(u)
        self.add_field('vendor_byte_list', self.u.unpack_rest())

class EndType(SmallResourceDescriptor):
    item_name = 0x0F

    def __init__(self, u):
        super(EndType, self).__init__(u)
        self.add_field('checksum', self.u.unpack_one("B"))
        self.fini()

class Memory24BitRangeDescriptor(LargeResourceDescriptor):
    item_name = 0x01

    def __init__(self, u):
        super(Memory24BitRangeDescriptor, self).__init__(u)
        self.add_field('_MIN', self.u.unpack_one("<H"))
        self.add_field('_MAX', self.u.unpack_one("<H"))
        self.add_field('_ALN', self.u.unpack_one("<H"))
        self.add_field('_LEN', self.u.unpack_one("<H"))
        self.fini()

class VendorDefinedLargeDescriptor(LargeResourceDescriptor):
    item_name = 0x04

    def __init__(self, u):
        super(VendorDefinedLargeDescriptor, self).__init__(u)
        self.add_field('uuid_sub_type', self.u.unpack_one("B"))
        self.add_field('uuid', uuid.UUID(bytes_le=u.unpack_one("16s")))
        self.add_field('vendor_byte_list', self.u.unpack_rest())

_write_statuses = {
    0: "non-writeable (read-only)",
    1: "writeable",
}

class Memory32BitRangeDescriptor(LargeResourceDescriptor):
    item_name = 0x05

    def __init__(self, u):
        super(Memory32BitRangeDescriptor, self).__init__(u)
        self.add_field('information', self.u.unpack_one("B"))
        self.add_field('_RW', bitfields.getbits(self.information, 0), unpack.format_table("information[0]={}", _write_statuses))
        self.add_field('_MIN', self.u.unpack_one("<I"))
        self.add_field('_MAX', self.u.unpack_one("<I"))
        self.add_field('_ALN', self.u.unpack_one("<I"))
        self.add_field('_LEN', self.u.unpack_one("<I"))
        self.fini()

class FixedMemory32BitRangeDescriptor(LargeResourceDescriptor):
    item_name = 0x06

    def __init__(self, u):
        super(FixedMemory32BitRangeDescriptor, self).__init__(u)
        self.add_field('information', self.u.unpack_one("B"))
        self.add_field('_RW', bitfields.getbits(self.information, 0), unpack.format_table("information[0]={}", _write_statuses))
        self.add_field('_BAS', self.u.unpack_one("<I"))
        self.add_field('_LEN', self.u.unpack_one("<I"))
        self.fini()

def _range_type_str(range_type):
    if range_type >= 192 and range_type <= 255:
        return 'OEM Defined'
    _range_types = {
        0: 'Memory range',
        1: 'IO range',
        2: 'Bus number range',
    }
    return _range_types.get(range_type, 'Reserved')

class DwordAddressSpaceDescriptor(LargeResourceDescriptor):
    item_name = 0x07

    def __init__(self, u):
        super(DwordAddressSpaceDescriptor, self).__init__(u)
        self.add_field('range_type', self.u.unpack_one("B"), unpack.format_function("{:#x}", _range_type_str))
        self.add_field('general_flags', self.u.unpack_one("B"))
        self.add_field('type_specific_flags', self.u.unpack_one("B"))
        self.add_field('address_space_granularity', self.u.unpack_one("<I"))
        self.add_field('address_range_minimum', self.u.unpack_one("<I"))
        self.add_field('address_range_maximum', self.u.unpack_one("<I"))
        self.add_field('address_translation_offset', self.u.unpack_one("<I"))
        self.add_field('address_length', self.u.unpack_one("<I"))
        self.fini()

class WordAddressSpaceDescriptor(LargeResourceDescriptor):
    item_name = 0x08

    def __init__(self, u):
        super(WordAddressSpaceDescriptor, self).__init__(u)
        self.add_field('range_type', self.u.unpack_one("B"), unpack.format_function("{:#x}", _range_type_str))
        self.add_field('general_flags', self.u.unpack_one("B"))
        self.add_field('type_specific_flags', self.u.unpack_one("B"))
        self.add_field('address_space_granularity', self.u.unpack_one("<H"))
        self.add_field('address_range_minimum', self.u.unpack_one("<H"))
        self.add_field('address_range_maximum', self.u.unpack_one("<H"))
        self.add_field('address_translation_offset', self.u.unpack_one("<H"))
        self.add_field('address_length', self.u.unpack_one("<H"))
        self.fini()

class ExtendedInterruptDescriptor(LargeResourceDescriptor):
    item_name = 0x09

    def __init__(self, u):
        super(ExtendedInterruptDescriptor, self).__init__(u)
        self.add_field('interrupt_vector_flags', self.u.unpack_one("B"))
        self.add_field('interrupt_table_length', self.u.unpack_one("B"))
        self.add_field('interrupt_number', tuple(self.u.unpack_one("<I") for i in range(self.interrupt_table_length)))
        self.fini()

class QwordAddressSpaceDescriptor(LargeResourceDescriptor):
    item_name = 0x0A

    def __init__(self, u):
        super(QwordAddressSpaceDescriptor, self).__init__(u)
        self.add_field('range_type', self.u.unpack_one("B"), unpack.format_function("{:#x}", _range_type_str))
        self.add_field('general_flags', self.u.unpack_one("B"))
        self.add_field('type_specific_flags', self.u.unpack_one("B"))
        self.add_field('address_space_granularity', self.u.unpack_one("<Q"))
        self.add_field('address_range_minimum', self.u.unpack_one("<Q"))
        self.add_field('address_range_maximum', self.u.unpack_one("<Q"))
        self.add_field('address_translation_offset', self.u.unpack_one("<Q"))
        self.add_field('address_length', self.u.unpack_one("<Q"))
        self.fini()

class ResourceDescriptorUnknown(BaseResourceDescriptor):
    resource_type = None
    item_name = None

    def __init__(self, u):
        super(ResourceDescriptorUnknown, self).__init__(u)
        self.fini()

_resource_descriptors = [
    IRQDescriptor,
    DMADescriptor,
    StartDependentFunctionsDescriptor,
    EndDependentFunctionsDescriptor,
    IOPortDescriptor,
    FixedIOPortDescriptor,
    FixedDMADescriptor,
    VendorDefinedSmallDescriptor,
    EndType,
    Memory24BitRangeDescriptor,
    VendorDefinedLargeDescriptor,
    Memory32BitRangeDescriptor,
    FixedMemory32BitRangeDescriptor,
    DwordAddressSpaceDescriptor,
    WordAddressSpaceDescriptor,
    ExtendedInterruptDescriptor,
    QwordAddressSpaceDescriptor,
    ResourceDescriptorUnknown, # Must always come last
]

class AcpiLocalReference(unpack.Struct):
    def __init__(self, local_reference):
        super(AcpiLocalReference, self).__init__()
        ActualType, NamePath = local_reference
        self.add_field('ActualType', ActualType)
        self.add_field('NamePath', NamePath)

class AcpiPower(unpack.Struct):
    def __init__(self, power_data):
        super(AcpiPower, self).__init__()
        SystemLevel, ResourceOrder = power_data
        self.add_field('SystemLevel', SystemLevel)
        self.add_field('ResourceOrder', ResourceOrder)

class AcpiProcessor(unpack.Struct):
    def __init__(self, processor_data):
        super(AcpiProcessor, self).__init__()
        ProcId, PblkAddress, PblkLength = processor_data
        self.add_field('ProcId', ProcId)
        self.add_field('PblkAddress', PblkAddress)
        self.add_field('PblkLength', PblkLength)

ACPI_TYPE_INTEGER = 0x01
ACPI_TYPE_STRING = 0x02
ACPI_TYPE_BUFFER = 0x03
ACPI_TYPE_PACKAGE = 0x04
ACPI_TYPE_FIELD_UNIT = 0x05
ACPI_TYPE_DEVICE = 0x06
ACPI_TYPE_EVENT = 0x07
ACPI_TYPE_METHOD = 0x08
ACPI_TYPE_MUTEX = 0x09
ACPI_TYPE_REGION = 0x0A
ACPI_TYPE_POWER = 0x0B
ACPI_TYPE_PROCESSOR = 0x0C
ACPI_TYPE_THERMAL = 0x0D
ACPI_TYPE_BUFFER_FIELD = 0x0E
ACPI_TYPE_DDB_HANDLE = 0x0F
ACPI_TYPE_DEBUG_OBJECT = 0x10
ACPI_TYPE_LOCAL_REFERENCE = 0x14

_acpi_object_types = {
    ACPI_TYPE_INTEGER: _id,
    ACPI_TYPE_STRING: _id,
    ACPI_TYPE_BUFFER: AcpiBuffer,
    ACPI_TYPE_PACKAGE: (lambda t: tuple(_acpi_object_to_python(v) for v in t)),
    ACPI_TYPE_POWER: AcpiPower,
    ACPI_TYPE_PROCESSOR: AcpiProcessor,
    ACPI_TYPE_LOCAL_REFERENCE: AcpiLocalReference,
}

def _acpi_object_to_python(acpi_object):
    if acpi_object is None:
        return None
    object_type, value = acpi_object
    return _acpi_object_types[object_type](value)

def _acpi_object_from_python(obj):
    if isinstance(obj, (int, long)):
        return (ACPI_TYPE_INTEGER, obj)
    # Must check AcpiBuffer before str, since AcpiBuffer derives from str
    if isinstance(obj, AcpiBuffer):
        return (ACPI_TYPE_BUFFER, obj)
    if isinstance(obj, str):
        return (ACPI_TYPE_STRING, obj)
    if isinstance(obj, AcpiPower):
        return (ACPI_TYPE_POWER, obj)
    if isinstance(obj, AcpiProcessor):
        return (ACPI_TYPE_PROCESSOR, obj)
    # Must check tuple after any namedtuples, since namedtuples derive from tuple
    if isinstance(obj, tuple):
        return (ACPI_TYPE_PACKAGE, obj)

def evaluate(pathname, *args):
    """Evaluate an ACPI method and return the result."""
    return _acpi_object_to_python(_acpi._eval(pathname, tuple(_acpi_object_from_python(arg) for arg in args)))

def get_object_info(pathname):
    """Get object information for an ACPI object."""
    return ObjectInfo(*_acpi._get_object_info(pathname))

acpi_object_types = {
    ACPI_TYPE_INTEGER: 'ACPI_TYPE_INTEGER',
    ACPI_TYPE_STRING: 'ACPI_TYPE_STRING',
    ACPI_TYPE_BUFFER: 'ACPI_TYPE_BUFFER',
    ACPI_TYPE_PACKAGE: 'ACPI_TYPE_PACKAGE',
    ACPI_TYPE_FIELD_UNIT: 'ACPI_TYPE_FIELD_UNIT',
    ACPI_TYPE_DEVICE: 'ACPI_TYPE_DEVICE',
    ACPI_TYPE_EVENT: 'ACPI_TYPE_EVENT',
    ACPI_TYPE_METHOD: 'ACPI_TYPE_METHOD',
    ACPI_TYPE_MUTEX: 'ACPI_TYPE_MUTEX',
    ACPI_TYPE_REGION: 'ACPI_TYPE_REGION',
    ACPI_TYPE_POWER: 'ACPI_TYPE_POWER',
    ACPI_TYPE_PROCESSOR: 'ACPI_TYPE_PROCESSOR',
    ACPI_TYPE_THERMAL: 'ACPI_TYPE_THERMAL',
    ACPI_TYPE_BUFFER_FIELD: 'ACPI_TYPE_BUFFER_FIELD',
    ACPI_TYPE_DDB_HANDLE: 'ACPI_TYPE_DDB_HANDLE',
    ACPI_TYPE_DEBUG_OBJECT: 'ACPI_TYPE_DEBUG_OBJECT',
    ACPI_TYPE_LOCAL_REFERENCE: 'ACPI_TYPE_LOCAL_REFERENCE',
}

class ObjectInfo(unpack.Struct):
    def __init__(self, data, addr):
        super(ObjectInfo, self).__init__()
        u = unpack.Unpackable(data)
        s = unpack.Unpackable(data)
        self.add_field('info_size', u.unpack_one("<I"))
        self.add_field('name', u.unpack_one("4s"))
        self.add_field('object_type', u.unpack_one("<I"), unpack.format_table("{}", acpi_object_types))
        self.add_field('parameter_count', u.unpack_one("B"))
        self.add_field('valid', u.unpack_one("B"))
        self.add_field('current_status_valid', bool(bitfields.getbits(self.valid, 0)), "valid[0]={}")
        self.add_field('address_valid', bool(bitfields.getbits(self.valid, 1)), "valid[1]={}")
        self.add_field('hardware_id_valid', bool(bitfields.getbits(self.valid, 2)), "valid[2]={}")
        self.add_field('unique_id_valid', bool(bitfields.getbits(self.valid, 3)), "valid[3]={}")
        self.add_field('subsystem_id_valid', bool(bitfields.getbits(self.valid, 4)), "valid[4]={}")
        self.add_field('compatibility_id_valid', bool(bitfields.getbits(self.valid, 5)), "valid[5]={}")
        self.add_field('highest_dstates_valid', bool(bitfields.getbits(self.valid, 6)), "valid[6]={}")
        self.add_field('lowest_dstates_valid', bool(bitfields.getbits(self.valid, 7)), "valid[7]={}")

        self.add_field('flags', u.unpack_one("B"))
        self.add_field('highest_dstates', tuple(u.unpack_one("B") for i in range(4)))
        self.add_field('lowest_dstates', tuple(u.unpack_one("B") for i in range(5)))
        self.add_field('current_status', u.unpack_one("<I"))

        if self.current_status_valid:
            self.add_field('present', bool(bitfields.getbits(self.current_status, 0)), "current_status[0]={}")
            self.add_field('enabled', bool(bitfields.getbits(self.current_status, 1)), "current_status[1]={}")
            self.add_field('visible', bool(bitfields.getbits(self.current_status, 2)), "current_status[2]={}")
            self.add_field('functional', bool(bitfields.getbits(self.current_status, 3)), "current_status[3]={}")
            self.add_field('battery_present', bool(bitfields.getbits(self.current_status, 4)), "current_status[4]={}")

        # Deal with padding before the 8-byte address field
        ptralign = struct.calcsize("I0P")
        if u.offset % ptralign != 0:
            u.skip(ptralign - (u.offset % ptralign))
        self.add_field('address', u.unpack_one("<Q"))

        def get_string():
            length, offset = u.unpack("IP")
            if not length:
                return None
            return s.unpack_peek_one("{}x{}s".format(offset - addr, length)).split("\x00", 1)[0]

        self.add_field('hardware_id', get_string())
        self.add_field('unique_id', get_string())
        self.add_field('subsystem_id', get_string())
        self.add_field('compatibility_id_count', u.unpack_one("<I"))
        self.add_field('compatibility_id_length', u.unpack_one("<I"))
        self.add_field('compatibility_ids', tuple(get_string() for i in range(self.compatibility_id_count)))

def scope(path):
    try:
        prefix, _ = path.rsplit('.', 1)
        return prefix
    except ValueError:
        return "/"

def parse_table(signature, instance=1):
    data = get_table(signature, instance)
    if data is None:
        return None
    return globals()[signature](data)

def make_compat_parser(signature):
    def parse(printflag=False, instance=1):
        table = parse_table(signature, instance)
        if table is None:
            return None
        if printflag:
            ttypager.ttypager_wrap(repr(table))
        return table
    return parse

class RSDP(unpack.Struct):
    def __init__(self, data):
        super(RSDP, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('signature', u.unpack_one("8s"))
        self.add_field('checksum', u.unpack_one("B"))
        self.add_field('oemid', u.unpack_one("6s"))
        self.add_field('revision', u.unpack_one("B"), "{}")
        self.add_field('rsdt_address', u.unpack_one("<I"))
        if self.revision >= 2:
            self.add_field('length', u.unpack_one("<I"), "{}")
            self.add_field('xsdt_address', u.unpack_one("<Q"))
            self.add_field('extended_checksum', u.unpack_one("B"))
            if self.revision == 2:
                reserved = u.unpack_raw(3)
            else:
                self.add_field('unknown', u.unpack_rest())

parse_rsdp = make_compat_parser("RSDP")

class SystemDescriptionTable(unpack.Struct):
    def __init__(self, data):
        super(SystemDescriptionTable, self).__init__()
        self.u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(self.u))

    def fini(self):
        del self.u

def format_table_addrs(addrs):
    return "(\n{})".format(",\n".join("{:#x} ({})".format(addr, bits.memory(addr, 4)) for addr in addrs))

class RSDT(SystemDescriptionTable):
    def __init__(self, data):
        super(RSDT, self).__init__(data)
        def _tables():
            while not self.u.at_end():
                yield self.u.unpack_one("<I")
        self.add_field('tables', tuple(_tables()), format_table_addrs)
        self.fini()

parse_rsdt = make_compat_parser("RSDT")

class XSDT(SystemDescriptionTable):
    def __init__(self, data):
        super(XSDT, self).__init__(data)
        def _tables():
            while not self.u.at_end():
                yield self.u.unpack_one("<Q")
        self.add_field('tables', tuple(_tables()), format_table_addrs)
        self.fini()

parse_xsdt = make_compat_parser("XSDT")

ACPI_DMAR_TYPE_DRHD = 0
ACPI_DMAR_TYPE_RMRR = 1
ACPI_DMAR_TYPE_ATSR = 2
ACPI_DMAR_TYPE_RHSA = 3

class DMAR(unpack.Struct):
    def __init__(self, data):
        super(DMAR, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('width', u.unpack_one("B"), "{}")
        self.add_field('flags', u.unpack_one("B"))
        self.add_field('intr_remap', bool(bitfields.getbits(self.flags, 0)), "flags[0]={}")
        u.skip(10)
        dmar_subtables = [
            DMARSubtableDRHD.unpack,
            DMARSubtableRMRR.unpack,
            DMARSubtableATSR.unpack,
            DMARSubtableRHSA.unpack,
            DMARSubtableUnknown.unpack, # Must always come last
        ]
        self.add_field('remappings', unpack.unpack_all(u, dmar_subtables), unpack.format_each("\n{!r}"))

parse_dmar = make_compat_parser("DMAR")

class DMARSubtable(unpack.Struct):
    @classmethod
    def unpack(cls, u):
        t, length = u.unpack_peek("<HH")
        if cls.dmar_subtable_type is not None and t != cls.dmar_subtable_type:
            return None
        return super(DMARSubtable, cls).unpack(u.unpack_unpackable(length))

class DMARSubtableDRHD(DMARSubtable):
    dmar_subtable_type = ACPI_DMAR_TYPE_DRHD

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("<H"), "{}"
        yield 'length', u.unpack_one("<H"), "{}"
        flags = u.unpack_one("B")
        yield 'flags', flags
        yield 'include_pci_all', bitfields.getbits(flags, 0)
        u.skip(1)
        yield 'segment_number', u.unpack_one("<H")
        yield 'base_address', u.unpack_one("<Q")
        yield 'device_scopes', unpack.unpack_all(u, [DMARDeviceScope.unpack]), unpack.format_each("\n{!r}")

class DMARSubtableRMRR(DMARSubtable):
    dmar_subtable_type = ACPI_DMAR_TYPE_RMRR

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("<H"), "{}"
        yield 'length', u.unpack_one("<H"), "{}"
        u.skip(2)
        yield 'segment_number', u.unpack_one("<H")
        yield 'base_address', u.unpack_one("<Q")
        yield 'limit_address', u.unpack_one("<Q")
        yield "device_scopes", unpack.unpack_all(u, [DMARDeviceScope.unpack]), unpack.format_each("\n{!r}")

class DMARSubtableATSR(DMARSubtable):
    dmar_subtable_type = ACPI_DMAR_TYPE_ATSR

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("<H"), "{}"
        yield 'length', u.unpack_one("<H"), "{}"
        flags = u.unpack_one("B")
        yield 'flags', flags
        yield 'all_ports', bitfields.getbits(flags, 0), "flags[0]={}"
        u.skip(1)
        yield "segment_number", u.unpack_one("<H")
        yield "device_scopes", unpack.unpack_all(u, [DMARDeviceScope.unpack]), unpack.format_each("\n{!r}")

class DMARSubtableRHSA(DMARSubtable):
    dmar_subtable_type = ACPI_DMAR_TYPE_RHSA

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("<H"), "{}"
        yield 'length', u.unpack_one("<H"), "{}"
        u.skip(4)
        yield 'base_address', "<Q"
        yield 'proximity_domain', "<I"

class DMARSubtableUnknown(DMARSubtable):
    dmar_subtable_type = None

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("<H"), "{}"
        yield 'length', u.unpack_one("<H"), "{}"
        yield 'data', u.unpack_rest(), lambda data: "\n" + bits.dumpmem(data)

class DMARDeviceScope(unpack.Struct):
    @classmethod
    def unpack(cls, u):
        length = u.unpack_peek_one("<xB")
        return super(DMARDeviceScope, cls).unpack(u.unpack_unpackable(length))

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("B"), "{}"
        yield 'length', u.unpack_one("B"), "{}"
        u.skip(2)
        yield 'enumeration_id', u.unpack_one("B")
        yield 'start_bus_number', u.unpack_one("B")
        yield 'paths', unpack.unpack_all(u, [DMARDeviceScopePath.unpack]), unpack.format_each("\n{!r}")

class DMARDeviceScopePath(unpack.Struct):
    @staticmethod
    def _unpack(u):
        yield 'pci_device', u.unpack_one("B")
        yield 'pci_function', u.unpack_one("B")

class GenericRegister(unpack.Struct):
    def __init__(self, register_data):
        super(GenericRegister, self).__init__()
        u = unpack.Unpackable(register_data)
        self.add_field('DescriptorType', u.unpack_one("B"), "{}")
        self.add_field('ResourceLength', u.unpack_one("<H"), "{}")
        self.add_field('AddressSpaceId', u.unpack_one("B"))
        self.add_field('BitWidth', u.unpack_one("B"))
        self.add_field('BitOffset', u.unpack_one("B"))
        self.add_field('AccessSize', u.unpack_one("B"))
        self.add_field('Address', u.unpack_one("<Q"))
        self.add_field('EndTagByte', u.unpack_one("B"))
        self.add_field('Checksum', u.unpack_one("B"))

class FixedFuncHwReg(unpack.Struct):
    def __new__(cls, register):
        if register.AddressSpaceId != 0x7f:
            return None
        return super(FixedFuncHwReg, cls).__new__(cls)

    def __init__(self, register):
        super(FixedFuncHwReg, self).__init__()
        self.add_field('Type', register.AddressSpaceId)
        self.add_field('VendorCode', register.BitWidth)
        self.add_field('ClassCode', register.BitOffset)
        self.add_field('Arg1', register.AccessSize)
        self.add_field('Arg0', register.Address)

class FACP(unpack.Struct):
    def __init__(self, data):
        super(FACP, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('firmware_ctrl', u.unpack_one("<I"))
        self.add_field('dsdt', u.unpack_one("<I"))
        u.skip(1)
        _preferred_pm_profile = {
            0: 'Unspecified',
            1: 'Desktop',
            2: 'Mobile',
            3: 'Workstation',
            4: 'Enterprise Server',
            5: 'SOHO Server',
            6: 'Appliance PC',
            7: 'Performance Server',
            8: 'Tablet'
        }
        self.add_field('preferred_pm_profile', u.unpack_one("B"), unpack.format_table("{}", _preferred_pm_profile))
        self.add_field('sci_int', u.unpack_one("<H"))
        self.add_field('smi_cmd', u.unpack_one("<I"))
        self.add_field('acpi_enable', u.unpack_one("B"))
        self.add_field('acpi_disable', u.unpack_one("B"))
        self.add_field('s4bios_req', u.unpack_one("B"))
        self.add_field('pstate_cnt', u.unpack_one("B"))
        self.add_field('pm1a_evt_blk', u.unpack_one("<I"))
        self.add_field('pm1b_evt_blk', u.unpack_one("<I"))
        self.add_field('pm1a_cnt_blk', u.unpack_one("<I"))
        self.add_field('pm1b_cnt_blk', u.unpack_one("<I"))
        self.add_field('pm2_cnt_blk', u.unpack_one("<I"))
        self.add_field('pm_tmr_blk', u.unpack_one("<I"))
        self.add_field('gpe0_blk', u.unpack_one("<I"))
        self.add_field('gpe1_blk', u.unpack_one("<I"))
        self.add_field('pm1_evt_len', u.unpack_one("B"))
        self.add_field('pm1_cnt_len', u.unpack_one("B"))
        self.add_field('pm2_cnt_len', u.unpack_one("B"))
        self.add_field('pm_tmr_len', u.unpack_one("B"))
        self.add_field('gpe0_blk_len', u.unpack_one("B"))
        self.add_field('gpe1_blk_len', u.unpack_one("B"))
        self.add_field('gpe1_base', u.unpack_one("B"))
        self.add_field('cst_cnt', u.unpack_one("B"))
        self.add_field('p_lvl2_lat', u.unpack_one("<H"))
        self.add_field('p_lvl3_lat', u.unpack_one("<H"))
        self.add_field('flush_size', u.unpack_one("<H"))
        self.add_field('flush_stride', u.unpack_one("<H"))
        self.add_field('duty_offset', u.unpack_one("B"))
        self.add_field('duty_width', u.unpack_one("B"))
        self.add_field('day_alrm', u.unpack_one("B"))
        self.add_field('mon_alrm', u.unpack_one("B"))
        self.add_field('century', u.unpack_one("B"))
        self.add_field('iapc_boot_arch', u.unpack_one("<H"))
        u.skip(1)
        self.add_field('flags', u.unpack_one("<I"))
        self.add_field('wbinvd', bool(bitfields.getbits(self.flags, 0)))
        self.add_field('wbinvd_flush', bool(bitfields.getbits(self.flags, 1)))
        self.add_field('proc_c1', bool(bitfields.getbits(self.flags, 2)))
        self.add_field('p_lvl2_up', bool(bitfields.getbits(self.flags, 3)))
        self.add_field('pwr_button', bool(bitfields.getbits(self.flags, 4)))
        self.add_field('slp_button', bool(bitfields.getbits(self.flags, 5)))
        self.add_field('fix_rtc', bool(bitfields.getbits(self.flags, 6)))
        self.add_field('rtc_s4', bool(bitfields.getbits(self.flags, 7)))
        self.add_field('tmr_val_ext', bool(bitfields.getbits(self.flags, 8)))
        if self.header.revision > 1:
            self.add_field('dck_cap', bool(bitfields.getbits(self.flags, 9)))
            self.add_field('reset_reg_sup', bool(bitfields.getbits(self.flags, 10)))
            self.add_field('sealed_case', bool(bitfields.getbits(self.flags, 11)))
            self.add_field('headless', bool(bitfields.getbits(self.flags, 12)))
            self.add_field('cpu_sw_slp', bool(bitfields.getbits(self.flags, 13)))
            self.add_field('pci_exp_wak', bool(bitfields.getbits(self.flags, 14)))
            self.add_field('use_platform_clock', bool(bitfields.getbits(self.flags, 15)))
            self.add_field('s4_rtc_sts_valid', bool(bitfields.getbits(self.flags, 16)))
            self.add_field('remote_power_on_capable', bool(bitfields.getbits(self.flags, 17)))
            self.add_field('force_apic_cluster_mode', bool(bitfields.getbits(self.flags, 18)))
            self.add_field('force_apic_physical_destination_mode', bool(bitfields.getbits(self.flags, 19)))
        if self.header.revision >= 5:
            self.add_field('hw_reduced_acpi', bool(bitfields.getbits(self.flags, 20)))
            self.add_field('low_power_s0_idle_capable', bool(bitfields.getbits(self.flags, 21)))
        if self.header.revision >= 2:
            self.add_field('reset_reg', GAS.unpack(u))
            self.add_field('reset_value', u.unpack_one("B"))
            u.skip(3)
            self.add_field('x_firmware_ctrl', u.unpack_one("<Q"))
            self.add_field('x_dsdt', u.unpack_one("<Q"))
            self.add_field('x_pm1a_evt_blk', GAS.unpack(u))
            self.add_field('x_pm1b_evt_blk', GAS.unpack(u))
            self.add_field('x_pm1a_cnt_blk', GAS.unpack(u))
            self.add_field('x_pm1b_cnt_blk', GAS.unpack(u))
            self.add_field('x_pm2_cnt_blk', GAS.unpack(u))
            self.add_field('x_pm_tmr_blk', GAS.unpack(u))
            self.add_field('x_gpe0_blk', GAS.unpack(u))
            self.add_field('x_gpe1_blk', GAS.unpack(u))
        if self.header.revision >= 5:
            self.add_field('sleep_control_reg', GAS.unpack(u))
            self.add_field('sleep_status_reg', GAS.unpack(u))

parse_facp = make_compat_parser("FACP")

class FACS(unpack.Struct):
    def __init__(self, data):
        super(FACS, self).__init__()
        u = unpack.Unpackable(data)
        version = u.unpack_peek_one("<32xB")
        self.add_field('signature', u.unpack_one("4s"))
        self.add_field('length', u.unpack_one("<I"), '{}')
        self.add_field('hardware_signature', u.unpack_one("<I"))
        self.add_field('firmware_waking_vector', u.unpack_one("<I"))
        global_lock = u.unpack_one("<I")
        self.add_field('global_lock', global_lock)
        self.add_field('pending', bool(bitfields.getbits(global_lock, 0)), "global_lock[0]={}")
        self.add_field('owned', bool(bitfields.getbits(global_lock, 1)), "global_lock[1]={}")
        flags = u.unpack_one("<I")
        self.add_field('flags', flags)
        self.add_field('s4bios_f', bool(bitfields.getbits(flags, 0)), "flags[0]={}")
        if version >= 2:
            self.add_field('64bit_wake_supported_f', bool(bitfields.getbits(flags, 1)), "flags[1]={}")
        if version >= 1:
            self.add_field('x_firmware_waking_vector', u.unpack_one("<Q"))
            self.add_field('version', u.unpack_one("B"))
        if version >= 2:
            u.skip(3)
            ospm_flags = u.unpack_one("<I")
            self.add_field('ospm_flags', ospm_flags)
            self.add_field('64bit_wake_f', bool(bitfields.getbits(ospm_flags, 0)), "ospm_flags[0]={}")
        # Eat the remaining bytes for completeness
        if version == 0:
            u.skip(40)
        elif version == 1:
            u.skip(31)
        elif version >= 2:
            u.skip(24)

parse_facs = make_compat_parser("FACS")

class MCFG(unpack.Struct):
    def __init__(self, data):
        super(MCFG, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        u.skip(8)
        self.add_field('resources', unpack.unpack_all(u, [MCFGResource.unpack]), unpack.format_each("\n{!r}"))

parse_mcfg = make_compat_parser("MCFG")

class MCFGResource(unpack.Struct):
    @staticmethod
    def _unpack(u):
        yield 'address', u.unpack_one("<Q")
        yield 'segment', u.unpack_one("<H")
        yield 'start_bus', u.unpack_one("B")
        yield 'end_bus', u.unpack_one("B")
        u.skip(4)

MADT_TYPE_LOCAL_APIC = 0
MADT_TYPE_IO_APIC = 1
MADT_TYPE_INT_SRC_OVERRIDE = 2
MADT_TYPE_NMI_INT_SRC = 3
MADT_TYPE_LOCAL_APIC_NMI = 4
MADT_TYPE_LOCAL_X2APIC = 9
MADT_TYPE_LOCAL_X2APIC_NMI = 0xA
MADT_TYPE_LOCAL_GIC = 0xB
MADT_TYPE_LOCAL_GIC_DISTRIBUTOR = 0xC

class APIC(unpack.Struct):
    def __init__(self, data):
        super(APIC, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('local_apic_address', u.unpack_one("<I"))
        self.add_field('flags', u.unpack_one("<I"))
        self.add_field('pcat_compat', bitfields.getbits(self.flags, 0), "flags[0]={}")
        subtables = unpack.unpack_all(u, _apic_subtables)
        procid_apicid = {}
        uid_x2apicid = {}
        for s in subtables:
            # accumulate the dictionaries
            if getattr(s, 'enabled', False):
                if s.type == MADT_TYPE_LOCAL_APIC:
                    procid_apicid[s.proc_id] = s.apic_id
                if s.type == MADT_TYPE_LOCAL_X2APIC:
                    uid_x2apicid[s.uid] = s.x2apicid
        self.add_field('subtables', subtables, unpack.format_each("\n{!r}"))
        self.add_field('procid_apicid', procid_apicid, "{!r}")
        self.add_field('uid_x2apicid', uid_x2apicid, "{!r}")

class _MAT(unpack.Struct):
    """Multiple APIC Table Entry"""
    def __init__(self, data):
        super(_MAT, self).__init__()
        self.add_field('subtables', unpack.unpack_all(unpack.Unpackable(data), _apic_subtables))

class APICSubtable(unpack.Struct):
    def __new__(cls, u):
        t, length = u.unpack_peek("<BB")
        if cls.apic_subtable_type is not None and t != cls.apic_subtable_type:
            return None
        return super(APICSubtable, cls).__new__(cls)

    def __init__(self, u):
        super(APICSubtable, self).__init__()
        length = u.unpack_peek_one("<xB")
        self.u = u.unpack_unpackable(length)
        self.raw_data = self.u.unpack_peek_one("{}s".format(length))
        self.add_field('type', self.u.unpack_one("B"))
        self.add_field('length', self.u.unpack_one("B"))

    def fini(self):
        if not self.u.at_end():
            self.add_field('data', self.u.unpack_rest())
        del self.u

class APICSubtableLocalApic(APICSubtable):
    apic_subtable_type = MADT_TYPE_LOCAL_APIC

    def __init__(self, u):
        super(APICSubtableLocalApic, self).__init__(u)
        self.add_field('proc_id', self.u.unpack_one("B"))
        self.add_field('apic_id', self.u.unpack_one("B"))
        self.add_field('flags', self.u.unpack_one("<I"))
        self.add_field('enabled', bool(bitfields.getbits(self.flags, 0)), "flags[0]={}")
        self.fini()

class APICSubtableIOApic(APICSubtable):
    apic_subtable_type = MADT_TYPE_IO_APIC

    def __init__(self, u):
        super(APICSubtableIOApic, self).__init__(u)
        self.add_field('io_apic_id', self.u.unpack_one("B"))
        self.u.skip(1)
        self.add_field('io_apic_addr', self.u.unpack_one("<I"))
        self.add_field('global_sys_int_base', self.u.unpack_one("<I"))
        self.fini()

mps_inti_polarity = {
    0b00: 'Conforms to bus specifications',
    0b01: 'Active high',
    0b11: 'Active low',
}

mps_inti_trigger_mode = {
    0b00: 'Conforms to bus specifications',
    0b01: 'Edge-triggered',
    0b11: 'Level-triggered',
}

class APICSubtableIntSrcOverride(APICSubtable):
    apic_subtable_type = MADT_TYPE_INT_SRC_OVERRIDE

    def __init__(self, u):
        super(APICSubtableIntSrcOverride, self).__init__(u)
        self.add_field('bus', self.u.unpack_one("B"))
        self.add_field('source', self.u.unpack_one("B"))
        self.add_field('global_sys_interrupt', self.u.unpack_one("<I"))
        self.add_field('flags', self.u.unpack_one("<H"))
        self.add_field('polarity', bitfields.getbits(self.flags, 1, 0), unpack.format_table("flags[1:0]={}", mps_inti_polarity))
        self.add_field('trigger_mode', bitfields.getbits(self.flags, 3, 2), unpack.format_table("flags[3:2]={}", mps_inti_trigger_mode))
        self.fini()

class APICSubtableNmiIntSrc(APICSubtable):
    apic_subtable_type = MADT_TYPE_NMI_INT_SRC

    def __init__(self, u):
        super(APICSubtableNmiIntSrc, self).__init__(u)
        self.add_field('flags', self.u.unpack_one("<H"))
        self.add_field('polarity', bitfields.getbits(self.flags, 1, 0), unpack.format_table("flags[1:0]={}", mps_inti_polarity))
        self.add_field('trigger_mode', bitfields.getbits(self.flags, 3, 2), unpack.format_table("flags[3:2]={}", mps_inti_trigger_mode))
        self.add_field('global_sys_interrupt', self.u.unpack_one("<I"))
        self.fini()

class APICSubtableLocalApicNmi(APICSubtable):
    apic_subtable_type = MADT_TYPE_LOCAL_APIC_NMI

    def __init__(self, u):
        super(APICSubtableLocalApicNmi, self).__init__(u)
        self.add_field('proc_id', self.u.unpack_one("B"))
        self.add_field('flags', self.u.unpack_one("<H"))
        self.add_field('polarity', bitfields.getbits(self.flags, 1, 0), unpack.format_table("flags[1:0]={}", mps_inti_polarity))
        self.add_field('trigger_mode', bitfields.getbits(self.flags, 3, 2), unpack.format_table("flags[3:2]={}", mps_inti_trigger_mode))
        self.add_field('lint_num', self.u.unpack_one("B"))
        self.fini()

class APICSubtableLocalx2Apic(APICSubtable):
    apic_subtable_type = MADT_TYPE_LOCAL_X2APIC

    def __init__(self, u):
        super(APICSubtableLocalx2Apic, self).__init__(u)
        self.u.skip(2)
        self.add_field('x2apicid', self.u.unpack_one("<I"))
        self.add_field('flags', self.u.unpack_one("<I"))
        self.add_field('enabled', bool(bitfields.getbits(self.flags, 0)), "flags[0]={}")
        self.add_field('uid', self.u.unpack_one("<I"))
        self.fini()

class APICSubtableLocalx2ApicNmi(APICSubtable):
    apic_subtable_type = MADT_TYPE_LOCAL_X2APIC_NMI

    def __init__(self, u):
        super(APICSubtableLocalx2ApicNmi, self).__init__(u)
        self.add_field('flags', self.u.unpack_one("<H"))
        self.add_field('polarity', bitfields.getbits(self.flags, 1, 0), unpack.format_table("flags[1:0]={}", mps_inti_polarity))
        self.add_field('trigger_mode', bitfields.getbits(self.flags, 3, 2), unpack.format_table("flags[3:2]={}", mps_inti_trigger_mode))
        self.add_field('uid', self.u.unpack_one("<I"))
        self.add_field('lint_num', self.u.unpack_one("B"))
        self.u.skip(3)
        self.fini()

class APICSubtableLocalGIC(APICSubtable):
    apic_subtable_type = MADT_TYPE_LOCAL_GIC

    def __init__(self, u):
        super(APICSubtableLocalGIC, self).__init__(u)
        self.u.skip(2)
        self.add_field('gic_id', self.u.unpack_one("<I"))
        self.add_field('uid', self.u.unpack_one("<I"))
        self.add_field('flags', self.u.unpack_one("<I"))
        self.add_field('enabled', bool(bitfields.getbits(self.flags, 0)), "flags[0]={}")
        _performance_interrupt_mode = {
            0: 'Level-triggered',
            1: 'Edge-triggered',
        }
        self.add_field('performance_interrupt_mode', bitfields.getbits(self.flags, 1), unpack.format_table("{}", _performance_interrupt_mode))
        self.add_field('parking_protocol_version', self.u.unpack_one("<I"), {})
        self.add_field('performance_interrupt_gsiv', self.u.unpack_one("<I"))
        self.add_field('parked_address', self.u.unpack_one("<Q"))
        self.add_field('physical_base_adddress', self.u.unpack_one("<Q"))
        self.fini()

class APICSubtableLocalGICDistributor(APICSubtable):
    apic_subtable_type = MADT_TYPE_LOCAL_GIC_DISTRIBUTOR

    def __init__(self, u):
        super(APICSubtableLocalGICDistributor, self).__init__(u)
        self.u.skip(2)
        self.add_field('gic_id', self.u.unpack_one("<I"))
        self.add_field('physical_base_adddress', self.u.unpack_one("<Q"))
        self.add_field('system_vector_base', self.u.unpack_one("<I"))
        self.u.skip(4)
        self.fini()

class APICSubtableUnknown(APICSubtable):
    apic_subtable_type = None

    def __init__(self, u):
        super(APICSubtableUnknown, self).__init__(u)
        self.fini()

_apic_subtables = [
    APICSubtableLocalApic,
    APICSubtableIOApic,
    APICSubtableIntSrcOverride,
    APICSubtableNmiIntSrc,
    APICSubtableLocalApicNmi,
    APICSubtableLocalx2Apic,
    APICSubtableLocalx2ApicNmi,
    APICSubtableLocalGIC,
    APICSubtableLocalGICDistributor,
    APICSubtableUnknown, # Must always come last
]

def parse_apic(printflag=False, EnabledOnly=False, instance=1):
    """Parse and optionally print an ACPI MADT table."""

    apic = parse_table("APIC", instance)
    if apic is None:
        return None, None
    if printflag:
        ttypager.ttypager_wrap(repr(apic))
    if EnabledOnly:
        s = ""
        for subtable in apic.subtables:
            if subtable.type in (MADT_TYPE_LOCAL_APIC, MADT_TYPE_LOCAL_X2APIC) and subtable.enabled:
                s += repr(subtable) + '\n'
        ttypager.ttypager_wrap(s)
    return (apic.procid_apicid, apic.uid_x2apicid)

PMTT_SOCKET = 0
PMTT_MEMORY_CONTROLLER = 1
PMTT_DIMM = 2

class PMTT(unpack.Struct):
    def __init__(self, data):
        super(PMTT, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        u.skip(4)
        pmtt_subtables = [
            PMTTSubtableSocket.unpack,
            PMTTSubtableMemController.unpack,
            PMTTSubtableDIMM.unpack,
            PMTTSubtableUnknown.unpack, # Must always come last
        ]
        self.add_field('subtables', unpack.unpack_all(u, pmtt_subtables), unpack.format_each("\n{!r}"))

parse_pmtt = make_compat_parser("PMTT")

class PMTTSubtable(unpack.Struct):
    @classmethod
    def unpack(cls, u):
        t, length = u.unpack_peek("<BxH")
        if cls.pmtt_subtable_type is not None and t != cls.pmtt_subtable_type:
            return None
        return super(PMTTSubtable, cls).unpack(u.unpack_unpackable(length))

class PMTTSubtableCommon(unpack.Struct):
    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("B"), "{}"
        u.skip(1)
        yield 'length', u.unpack_one("<H"), "{}"
        flags = u.unpack_one("<H")
        yield 'flags', flags
        yield 'top_level_aggregator_device', bool(bitfields.getbits(flags, 0)), "flags[0]={}"
        yield 'physical_topology_element', bool(bitfields.getbits(flags, 1)), "flags[1]={}"
        _component_memory_type = {
            0b00:   'Volatile memory',
            0b01:   'Both volatile and non-volatile memory',
            0b10:   'Non-volatile memory',
        }
        yield 'component_memory_type', bitfields.getbits(flags, 3, 2), unpack.format_table("flags[3:2]={}", _component_memory_type)
        u.skip(2)

class PMTTSubtableSocket(PMTTSubtable):
    pmtt_subtable_type = PMTT_SOCKET

    @staticmethod
    def _unpack(u):
        yield 'subtable_common', PMTTSubtableCommon.unpack(u)
        yield 'socket_identifier', u.unpack_one("<H")
        u.skip(2)
        yield 'resources', unpack.unpack_all(u, [PMTTSubtableMemController.unpack]), unpack.format_each("\n{!r}")

class PMTTSubtableMemController(PMTTSubtable):
    pmtt_subtable_type = PMTT_MEMORY_CONTROLLER

    @staticmethod
    def _unpack(u):
        yield 'subtable_common', PMTTSubtableCommon.unpack(u)
        yield 'read_latency', u.unpack_one("<I")
        yield 'write_latency', u.unpack_one("<I")
        yield 'read_bandwidth', u.unpack_one("<I")
        yield 'write_bandwidth', u.unpack_one("<I")
        yield 'optimal_access_unit', u.unpack_one("<H")
        yield 'optimal_access_aligment', u.unpack_one("<H")
        u.skip(2)
        number_proximity_domains = u.unpack_one("<H")
        yield 'number_proximity_domains', number_proximity_domains
        yield 'domains', tuple(u.unpack_one("<I") for i in range(number_proximity_domains))
        yield 'subtables', unpack.unpack_all(u, [PMTTSubtableDIMM.unpack]), unpack.format_each("\n{!r}")

class PMTTSubtableDIMM(PMTTSubtable):
    pmtt_subtable_type = PMTT_DIMM

    @staticmethod
    def _unpack(u):
        yield 'subtable_common', PMTTSubtableCommon.unpack(u)
        yield 'physical_component_id', u.unpack_one("<H")
        u.skip(2)
        yield 'dimm_size', u.unpack_one("<I")
        yield 'smbios_handle' ,u.unpack_one("<I")

class PMTTSubtableUnknown(PMTTSubtable):
    pmtt_subtable_type = None

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("B"), "{}"
        u.skip(1)
        yield 'length', u.unpack_one("<H"), "{}"
        yield 'data', u.unpack_rest(), lambda data: "\n" + bits.dumpmem(data)

class MPST(unpack.Struct):
    def __init__(self, data):
        super(MPST, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('pcc_id', u.unpack_one("B"))
        u.skip(3)
        memory_power_node_count = u.unpack_one("<H")
        self.add_field('memory_power_node_count', memory_power_node_count)
        u.skip(2)
        self.add_field('memory_power_nodes', tuple(MPSTMemPowerNode.unpack(u) for i in range(memory_power_node_count)), unpack.format_each("\n{!r}"))
        characteristics_count = u.unpack_one("<H")
        self.add_field('characteristics_count', characteristics_count, "{}")
        u.skip(2)
        self.add_field('characteristics', tuple(MPSTCharacteristics.unpack(u) for i in range(characteristics_count)), unpack.format_each("\n{!r}"))

parse_mpst = make_compat_parser("MPST")

class MPSTMemPowerNode(unpack.Struct):
    @staticmethod
    def _unpack(u):
        flags = u.unpack_one("B")
        yield 'flags', flags
        yield 'enabled', bool(bitfields.getbits(flags, 0)), "flags[0]={}"
        yield 'power_managed', bool(bitfields.getbits(flags, 1)), "flags[1]={}"
        yield 'hot_pluggable', bool(bitfields.getbits(flags, 2)), "flags[2]={}"
        u.skip(1)
        yield 'node_id', u.unpack_one("<H")
        yield 'length', u.unpack_one("<I")
        yield 'base_address_low', u.unpack_one("<I")
        yield 'base_address_high', u.unpack_one("<I")
        yield 'length_low', u.unpack_one("<I")
        yield 'length_high', u.unpack_one("<I")
        num_power_states = u.unpack_one("<I")
        yield 'num_power_states', num_power_states
        num_physical_components = u.unpack_one("<I")
        yield 'num_physical_components', num_physical_components
        yield 'memory_power_nodes', tuple(MPSTState.unpack(u) for i in range(num_power_states)), unpack.format_each("\n{!r}")
        yield 'physical_component_ids', tuple(u.unpack_one("<H") for i in range(num_physical_components))

class MPSTState(unpack.Struct):
    @staticmethod
    def _unpack(u):
        yield 'value', u.unpack_one("B")
        yield 'information_index', u.unpack_one("B")

class MPSTCharacteristics(unpack.Struct):
    @staticmethod
    def _unpack(u):
        pss_id = u.unpack_one("B")
        yield 'pss_id', pss_id
        yield 'pss_id_value', bitfields.getbits(pss_id, 5, 0), "pss_id[5:0]={}"
        yield 'pss_id_revision', bitfields.getbits(pss_id, 7, 6), "pss_id[7:6]={}"
        flags = u.unpack_one("B")
        yield 'flags', flags
        yield 'memory_content_preserved', bool(bitfields.getbits(flags, 0)), "flags[0]={}"
        yield 'autonomous_power_state_entry', bool(bitfields.getbits(flags, 1)), "flags[1]={}"
        yield 'autonomous_power_state_exit', bool(bitfields.getbits(flags, 2)), "flags[2]={}"
        u.skip(2)
        yield 'average_power', u.unpack_one("<I")
        yield 'relative_power', u.unpack_one("<I")
        yield 'exit_latency', u.unpack_one("<Q")
        u.skip(8)

class MSCT(unpack.Struct):
    def __init__(self, data):
        super(MSCT, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('proximity_domain_info_offset', u.unpack_one("<I"))
        self.add_field('max_proximity_domains', u.unpack_one("<I"))
        self.add_field('max_clock_domains', u.unpack_one("<I"))
        self.add_field('max_physical_address', u.unpack_one("<Q"))
        self.add_field('subtables', unpack.unpack_all(u, [MSCTProximityDomainInfo.unpack]), unpack.format_each("\n{!r}"))

parse_msct = make_compat_parser("MSCT")

class MSCTProximityDomainInfo(unpack.Struct):
    @staticmethod
    def _unpack(u):
        yield 'revision', u.unpack_one("B"), "{}"
        yield 'length', u.unpack_one("B"), "{}"
        yield 'proximity_domain_range_low', u.unpack_one("<I")
        yield 'proximity_domain_range_high', u.unpack_one("<I")
        yield 'max_processor_capacity', u.unpack_one("<I"), "{}"
        yield 'max_memory_capacity', u.unpack_one("<Q")

class MSDM(unpack.Struct):
    def __init__(self, data):
        super(MSDM, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('software_licensing_structure', u.unpack_rest(), lambda data: "\n" + bits.dumpmem(data))

parse_msdm = make_compat_parser("MSDM")

class SLIC(unpack.Struct):
    def __init__(self, data):
        super(SLIC, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('software_licensing_structure', u.unpack_rest(), lambda data: "\n" + bits.dumpmem(data))

parse_slic = make_compat_parser("SLIC")

class SLIT(unpack.Struct):
    def __init__(self, data):
        super(SLIT, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('number_system_localities', u.unpack_one("<Q"))
        self.add_field('relative_distances', tuple(tuple(u.unpack_one("B") for j in range(self.number_system_localities)) for i in range(self.number_system_localities)))

parse_slit = make_compat_parser("SLIT")

SRAT_LOCAL_APIC_AFFINITY = 0
SRAT_MEMORY_AFFINITY = 1
SRAT_LOCAL_X2APIC_AFFINITY = 2

class SRAT(unpack.Struct):
    def __init__(self, data):
        super(SRAT, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        u.skip(4)
        u.skip(8)
        srat_subtables = [
            SRATLocalApicAffinity.unpack,
            SRATMemoryAffinity.unpack,
            SRATLocalX2ApicAffinity.unpack,
            SRATSubtableUnknown.unpack, # Must always come last
        ]
        self.add_field('subtables', unpack.unpack_all(u, srat_subtables), unpack.format_each("\n{!r}"))

class SRATSubtable(unpack.Struct):
    @classmethod
    def unpack(cls, u):
        t, length = u.unpack_peek("<BB")
        if cls.srat_subtable_type is not None and t != cls.srat_subtable_type:
            return None
        return super(SRATSubtable, cls).unpack(u.unpack_unpackable(length))

class SRATLocalApicAffinity(SRATSubtable):
    srat_subtable_type = SRAT_LOCAL_APIC_AFFINITY

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("B")
        yield 'length', u.unpack_one("B")
        proximity_domain_7_0 = u.unpack_one("B")
        yield 'proximity_domain_7_0', proximity_domain_7_0
        yield 'apic_id', u.unpack_one("B")
        flags = u.unpack_one("<I")
        yield 'flags', flags
        yield 'enabled', bool(bitfields.getbits(flags, 0)), "flags[0]={}"
        yield 'local_sapic_eid', u.unpack_one("B")
        pd = u.unpack("3B")
        proximity_domain_31_8 = (pd[0] << 16) + (pd[1] << 8) + pd[2]
        yield 'proximity_domain_31_8', proximity_domain_31_8
        yield 'proximity_domain', (proximity_domain_31_8 << 8) + proximity_domain_7_0
        yield 'clock_domain', u.unpack_one("<I")

class SRATMemoryAffinity(SRATSubtable):
    srat_subtable_type = SRAT_MEMORY_AFFINITY

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("B"), "{}"
        yield 'length', u.unpack_one("B"), "{}"
        yield 'proximity_domain', u.unpack_one("<I")
        u.skip(2)
        yield 'base_address_low', u.unpack_one("<I")
        yield 'base_address_high', u.unpack_one("<I")
        yield 'length_low', u.unpack_one("<I")
        yield 'length_high', u.unpack_one("<I")
        u.skip(4)
        flags = u.unpack_one("<I")
        yield 'flags', flags
        yield 'enabled', bool(bitfields.getbits(flags, 0)), "flags[0]={}"
        yield 'hot_pluggable', bool(bitfields.getbits(flags, 1)), "flags[1]={}"
        yield 'nonvolatile', bool(bitfields.getbits(flags, 2)), "flags[2]={}"
        u.skip(8)

class SRATLocalX2ApicAffinity(SRATSubtable):
    srat_subtable_type = SRAT_LOCAL_X2APIC_AFFINITY

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("B"), "{}"
        yield 'length', u.unpack_one("B"), "{}"
        u.skip(2)
        yield 'proximity_domain', u.unpack_one("<I")
        yield 'x2apic_id', u.unpack_one("<I")
        flags = u.unpack_one("<I")
        yield 'flags', flags
        yield 'enabled', bool(bitfields.getbits(flags, 0)), "flags[0]={}"
        yield 'clock_domain', u.unpack_one("<I")
        u.skip(4)

class SRATSubtableUnknown(SRATSubtable):
    srat_subtable_type = None

    @staticmethod
    def _unpack(u):
        yield 'type', u.unpack_one("B"), "{}"
        yield 'length', u.unpack_one("B"), "{}"
        yield 'data', u.unpack_rest(), lambda data: "\n" + bits.dumpmem(data)

def parse_srat(printflag=False, EnabledOnly=False, instance=1):
    """Parse and optionally print an SRAT table."""

    srat = parse_table("SRAT", instance)
    if srat is None:
        return None
    if printflag:
        ttypager.ttypager_wrap(repr(srat))
    if EnabledOnly:
        s = ""
        for subtable in srat.subtables:
            if getattr(subtable, "enabled", False):
                s += repr(subtable) + '\n'
        ttypager.ttypager_wrap(s)
    return srat

ASID_SYSTEM_MEMORY = 0
ASID_SYSTEM_IO = 1
ASID_PCI_CFG_SPACE = 2
ASID_EMBEDDED_CONTROLLER = 3
ASID_SMBUS = 4
ASID_PCC = 0xA
ASID_FFH = 0x7F

def _asid_str(asid):
    if asid >= 0xC0 and asid <= 0xff:
        return 'OEM Defined'
    _asid = {
        ASID_SYSTEM_MEMORY: 'System Memory',
        ASID_SYSTEM_IO: 'System IO',
        ASID_PCI_CFG_SPACE: 'PCI Configuration Space',
        ASID_EMBEDDED_CONTROLLER: 'Embedded Controller',
        ASID_SMBUS: 'SMBus',
        ASID_PCC: 'Platform Communications Channel (PCC)',
        ASID_FFH: 'Functional Fixed Hardware',
        }
    return _asid.get(asid, 'Reserved')

class GAS(unpack.Struct):
    @staticmethod
    def _unpack(u):
        yield 'address_space_id', u.unpack_one("B"), unpack.format_function("{:#x}", _asid_str)
        yield 'register_bit_width', u.unpack_one("B")
        yield 'register_bit_offset', u.unpack_one("B")
        _access_sizes = {
            0: 'Undefined',
            1: 'Byte access',
            2: 'Word access',
            3: 'Dword access',
            4: 'Qword access',
        }
        yield 'access_size', u.unpack_one("B"), unpack.format_table("{}", _access_sizes)
        yield 'address', u.unpack_one("<Q")

class SPCR(SystemDescriptionTable):
    def __init__(self, data):
        super(SPCR, self).__init__(data)
        self.add_field('interface_type', self.u.unpack_one("B"), "{}")
        self.u.skip(3)
        self.add_field('base_address', GAS.unpack(self.u))
        self.add_field('int_type', self.u.unpack_one("B"))
        self.add_field('irq', self.u.unpack_one("B"))
        self.add_field('global_sys_int', self.u.unpack_one("<I"))
        self.add_field('baud_rate', self.u.unpack_one("B"), "{}")
        # Decode for baud rate the BIOS used for redirection
        _baud = {
            3: 9600,
            4: 19200,
            6: 57600,
            7: 115200,
        }
        self.add_field('baud_rate_decode', _baud.get(self.baud_rate), unpack.reserved_None())
        self.add_field('parity', self.u.unpack_one("B"), unpack.format_table("{}", { 0: 'No Parity' }))
        self.add_field('stop_bits', self.u.unpack_one("B"), unpack.format_table("{}", { 1: '1 stop bit' }))
        self.add_field('flow_control', self.u.unpack_one("B"), "{}")
        self.add_field('DCD', bool(bitfields.getbits(self.flow_control, 0)))
        self.add_field('RTSCTS', bool(bitfields.getbits(self.flow_control, 1)))
        self.add_field('XONXOFF', bool(bitfields.getbits(self.flow_control, 2)))
        self.add_field('terminal_type', self.u.unpack_one("B"), "{}")
        self.u.skip(1)
        self.add_field('pci_did', self.u.unpack_one("<H"))
        self.add_field('pci_vid', self.u.unpack_one("<H"))
        self.add_field('pci_bus', self.u.unpack_one("B"))
        self.add_field('pci_dev', self.u.unpack_one("B"))
        self.add_field('pci_func', self.u.unpack_one("B"))
        self.add_field('pci_flags', self.u.unpack_one("<I"))
        self.add_field('pci_segment', self.u.unpack_one("B"))
        self.u.skip(4)
        self.fini()

parse_spcr = make_compat_parser("SPCR")

class HPET(SystemDescriptionTable):
    def __init__(self, data):
        super(HPET, self).__init__(data)
        self.add_field('event_timer_block_id', self.u.unpack_one("<I"))
        self.add_field('pci_vid', bitfields.getbits(self.event_timer_block_id, 31, 16), "event_timer_block_id[31:16]={:#x}")
        self.add_field('legacy_replacement_IRQ_routing_capable', bool(bitfields.getbits(self.event_timer_block_id, 15)), "event_timer_block_id[15]={}")
        self.add_field('count_size_cap_counter_size', bitfields.getbits(self.event_timer_block_id, 13), "event_timer_block_id[13]={}")
        self.add_field('num_comparators', bitfields.getbits(self.event_timer_block_id, 12, 8), "event_timer_block_id[12:8]={:#x}")
        self.add_field('hardware_rev_id', bitfields.getbits(self.event_timer_block_id, 7, 0), "event_timer_block_id[7:0]={:#x}")
        self.add_field('base_address', GAS.unpack(self.u))
        self.add_field('hpet_number', self.u.unpack_one("B"), "{}")
        self.add_field('main_counter_min_clock_tick_in_periodic_mode', self.u.unpack_one("<H"), "{}")
        self.add_field('capabilities', self.u.unpack_one("B"))
        _page_protection_table = {
            0: 'No Guarantee for page protection',
            1: '4KB page protected',
            2: '64KB page protected',
        }
        self.add_field('page_protection', bitfields.getbits(self.capabilities, 3, 0), unpack.format_table("capabilities[3:0]={:#x}", _page_protection_table))
        self.add_field('oem_attributes', bitfields.getbits(self.capabilities, 7, 4), "capabilities[7:4]={:#x}")
        self.fini()

parse_hpet = make_compat_parser("HPET")

class WDDT(unpack.Struct):
    def __init__(self, data):
        super(WDDT, self).__init__()
        u = unpack.Unpackable(data)
        self.add_field('header', TableHeader(u))
        self.add_field('tco_spec_version', u.unpack_one("<H"))
        self.add_field('tco_description_table_version', u.unpack_one("<H"))
        self.add_field('pci_vid', u.unpack_one("<H"))
        self.add_field('tco_base_address', GAS.unpack(u))
        self.add_field('timer_min_count', u.unpack_one("<H"))
        self.add_field('timer_max_count', u.unpack_one("<H"))
        self.add_field('timer_count_period', u.unpack_one("<H"))
        self.add_field('status', u.unpack_one("<H"))
        _wdt_available_decode = {
            0: 'permanently disabled',
            1: 'available',
        }
        self.add_field('wdt_available', bitfields.getbits(self.status,0), unpack.format_table("{}", _wdt_available_decode))
        _wdt_active_decode = {
            0: 'WDT stopped when BIOS hands off control',
            1: 'WDT running when BIOS hnads off control',
        }
        self.add_field('wdt_active', bitfields.getbits(self.status,1), unpack.format_table("{}", _wdt_active_decode))
        _ownership_decode = {
            0: 'TCO is owned by the BIOS',
            1: 'TCO is owned by the OS',
        }
        self.add_field('ownership', bitfields.getbits(self.status,2), unpack.format_table("{}", _ownership_decode))
        self.add_field('user_reset_event', bool(bitfields.getbits(self.status,11)))
        self.add_field('wdt_event', bool(bitfields.getbits(self.status,12)))
        self.add_field('power_fail_event', bool(bitfields.getbits(self.status,13)))
        self.add_field('unknown_reset_event', bool(bitfields.getbits(self.status,14)))
        self.add_field('capability', u.unpack_one("<H"))
        self.add_field('auto_reset', bool(bitfields.getbits(self.capability,0)))
        self.add_field('alert_support', bool(bitfields.getbits(self.capability,1)))
        self.add_field('platform_directed_shutdown', bool(bitfields.getbits(self.capability,2)))
        self.add_field('immediate_shutdown', bool(bitfields.getbits(self.capability,3)))
        self.add_field('bios_handoff_support', bool(bitfields.getbits(self.capability,4)))

parse_wddt = make_compat_parser("WDDT")

class TableHeader(unpack.Struct):
    def __init__(self, u):
        super(TableHeader, self).__init__()
        self.add_field('signature', u.unpack_one("4s"))
        self.add_field('length', u.unpack_one("<I"), "{}")
        self.add_field('revision', u.unpack_one("B"), "{}")
        self.add_field('checksum', u.unpack_one("B"))
        self.add_field('oemid', u.unpack_one("<6s"))
        self.add_field('oemtableid', u.unpack_one("<8s"))
        self.add_field('oemrevision', u.unpack_one("<I"))
        self.add_field('creatorid', u.unpack_one("<4s"))
        self.add_field('creatorrevision', u.unpack_one("<I"))

    def repack(self):
        return struct.pack("<4sIBB6s8sI4sI", *self)

def get_cpupaths(*args):
    cpupaths, devpaths = _acpi._cpupaths(*args)
    procid_apicid, uid_x2apicid = parse_apic()
    if procid_apicid is None or uid_x2apicid is None:
        # No APIC table exists, so assume the existing cpus are enabled
        return cpupaths
    enabled_cpupaths = []
    for cpupath in cpupaths + devpaths:
        procdef = evaluate(cpupath)
        uid = evaluate(cpupath + "._UID")
        if (procdef is not None and procdef.ProcId in procid_apicid) or (uid is not None and uid in uid_x2apicid):
           enabled_cpupaths.append(cpupath)
    return enabled_cpupaths

def find_procid():
    cpupaths = get_cpupaths()
    cpupath_procid = {}
    for cpupath in cpupaths:
        processor = evaluate(cpupath)
        if processor is not None:
            cpupath_procid[cpupath] = processor.ProcId
        else:
            cpupath_procid[cpupath] = None
    return OrderedDict(sorted(cpupath_procid.items()))

def find_uid():
    cpupaths = get_cpupaths()
    cpupath_uid = {}
    for cpupath in cpupaths:
        value = evaluate(cpupath + "._UID")
        cpupath_uid[cpupath] = value
    return OrderedDict(sorted(cpupath_uid.items()))

def commonprefix(l):
    """Return the common prefix of a list of strings."""
    if not l:
        return ''
    prefix = l[0]
    for s in l[1:]:
        for i, c in enumerate(prefix):
            if c != s[i]:
                prefix = s[:i]
                break
    return prefix

def factor_commonprefix(l):
    if not l:
        return ''
    if len(l) == 1:
        return l[0]
    prefix = commonprefix(l)
    prefixlen = len(prefix)
    return prefix + "{" + ", ".join([s[prefixlen:] for s in l]) + "}"

def display_cpu_info():
    cpupaths = get_cpupaths()
    cpupath_procid = find_procid()
    cpupath_uid = find_uid()
    procid_apicid, uid_x2apicid = parse_apic()
    if procid_apicid is None or uid_x2apicid is None:
        return
    socketindex_cpuscope = {}
    s = factor_commonprefix(cpupaths) + '\n'
    for cpupath in cpupaths:
        s += '\n' + cpupath
        def socket_str(apicid):
            socket_index = bits.socket_index(apicid)
            if socket_index is None:
                return ''
            return ', socketIndex=0x%02x' % socket_index
        def apicid_str(apicid):
            if apicid is None:
                return 'no ApicID'
            return 'ApicID=0x%02x%s' % (apicid, socket_str(apicid))
        procid = cpupath_procid.get(cpupath, None)
        if procid is not None:
            s += ' ProcID=%-2u (%s) ' % (procid, apicid_str(procid_apicid.get(procid, None)))
        uid = cpupath_uid.get(cpupath, None)
        if uid is not None:
            s += '_UID=%s (%s)' % (uid, apicid_str(uid_x2apicid.get(uid, None)))
        socketindex_cpuscope.setdefault(bits.socket_index(procid_apicid.get(procid, None)), []).append(scope(cpupath))
    for value, scopes in socketindex_cpuscope.iteritems():
        unique_scopes = set(scopes)
        s += '\nsocket {0} contains {1} processors and {2} ACPI scope: {3}\n'.format(value, len(scopes), len(unique_scopes), ','.join(sorted(unique_scopes)))
    ttypager.ttypager_wrap(s, indent=False)

def display_acpi_method(method, print_one):
    """Helper function that performs all basic processing for evaluating an ACPI method"""
    cpupaths = get_cpupaths()
    uniques = {}
    for cpupath in cpupaths:
        value = evaluate(cpupath + "." + method)
        uniques.setdefault(value, []).append(cpupath)

    print ttypager._wrap("%u unique %s values" % (len(uniques), method))
    for value, cpupaths in sorted(uniques.iteritems(), key=(lambda (k,v): v)):
        print
        print ttypager._wrap(factor_commonprefix(cpupaths))
        if value is None:
            print "No %s found for these CPUs" % method
        else:
            print_one(value)

def parse_cpu_method(method):
    cls = globals()[method]
    cpupaths = get_cpupaths()
    uniques = {}
    for cpupath in cpupaths:
        value = evaluate(cpupath + "." + method)
        if value is not None:
            obj = cls(value)
        else:
            obj = None
        uniques.setdefault(obj, []).append(cpupath)
    return uniques

def display_cpu_method(method):
    uniques = parse_cpu_method(method)
    lines = [ttypager._wrap("{} unique {} values".format(len(uniques), method))]
    for value, cpupaths in sorted(uniques.iteritems(), key=(lambda (k,v): v)):
        lines.append("")
        lines.append(ttypager._wrap(factor_commonprefix(cpupaths)))
        if value is None:
            lines.append("No {} found for these CPUs".format(method))
        else:
            lines.extend(ttypager._wrap(repr(value), indent=False).splitlines())
    ttypager.ttypager("\n".join(lines))

class CSDDependency(unpack.Struct):
    def __init__(self, csd_data):
        super(CSDDependency, self).__init__()
        num_entries, revision, domain, coordination_type, num_processors, index = csd_data
        self.add_field('num_entries', num_entries)
        self.add_field('revision', revision)
        self.add_field('domain', domain)
        self.add_field('coordination_type', coordination_type)
        self.add_field('num_processors', num_processors)
        self.add_field('index', index)

class _CSD(unpack.Struct):
    """C-State Dependency"""
    def __init__(self, csd_data):
        super(_CSD, self).__init__()
        self.add_field('dependencies', tuple(map(CSDDependency, csd_data)))

    def __repr__(self):
        if not self.dependencies:
            return "Empty _CSD"
        lines = ["                 {:<10s} {:<10s} {:<10s} {:<10s} {:<13s} {:<10s}".format("NumEntries", "Revision", "Dom", "CoordType", "NumProcessors", "Index")]
        for csd_num, dependency in enumerate(self.dependencies):
            lines.append("Dependency[{csd_num}]: {c.num_entries:<#10x} {c.revision:<#10x} {c.domain:<#10x} {c.coordination_type:<#10x} {c.num_processors:<#13x} {c.index:<#8x}".format(csd_num=csd_num, c=dependency))
        return "\n".join(lines)

class _CST(unpack.Struct):
    """C-States"""
    def __init__(self, cst_data):
        super(_CST, self).__init__()
        self.add_field('count', cst_data[0])
        self.add_field('cstates', tuple(map(CState, cst_data[1:])), unpack.format_each("\n{!r}"))

    def __repr__(self):
        if not self.cstates:
            return "Empty _CST"
        lines = []
        for cstate_num, cstate in enumerate(self.cstates):
            desc = ''
            if hasattr(cstate, 'ffh'):
                # Decode register as FFH
                if cstate.ffh.VendorCode == 0:
                    desc += "C1 Halt"
                elif (cstate.ffh.VendorCode == 1) and (cstate.ffh.ClassCode == 1):
                    desc += "C1 I/O then Halt I/O port address = {#x}".format(Register.Arg0)
                elif (cstate.ffh.VendorCode == 1) and (cstate.ffh.ClassCode == 2):
                    desc += "MWAIT {:#02x} ({})".format(cstate.ffh.Arg0, cpulib.mwait_hint_to_cstate(cstate.ffh.Arg0))
                    desc += " {} {}BMAvoid".format(("SWCoord", "HWCoord")[bool(cstate.ffh.Arg1 & 1)], ("!", "")[bool(cstate.ffh.Arg1 & (1 << 1))])
                lines.append("C{cstate_num:<d}  {desc}".format(cstate_num=cstate_num, desc=desc))
            else:
                # Decode register as actual hardware resource
                lines.append("    {:11s} {:10s} {:9s} {:10s} {:8s}".format("AddrSpaceId", "BitWidth", "BitOffset", "AccessSize", "Address"))
                lines.append("C{cstate_num:<d}  {r.AddressSpaceId:<#11x} {r.BitWidth:<#10x} {r.BitOffset:<#9x} {r.AccessSize:<#10x} {r.Address:<#8x}".format(cstate_num=cstate_num, r=cstate.register))
            # Decode and print ACPI c-state, latency, & power
            lines.append("    ACPI C{c.type:<1d}  latency={c.latency}us  power={c.power}mW".format(c=cstate))
        return "\n".join(lines)

class CState(unpack.Struct):
    def __init__(self, cstate_data):
        super(CState, self).__init__()
        reg_data, type, latency, power = cstate_data
        register = GenericRegister(reg_data)
        self.add_field('register', register, "\n{!r}")
        ffh = FixedFuncHwReg(register)
        if ffh is not None:
            self.add_field('ffh', ffh, "\n{!r}")
        self.add_field('type', type)
        self.add_field('latency', latency, "{} us")
        self.add_field('power', power, "{} mW")

class _PCT(unpack.Struct):
    """Performance Control"""
    def __init__(self, pct_data):
        super(_PCT, self).__init__()
        self.add_field('registers', tuple(map(GenericRegister, pct_data)))

    def __repr__(self):
        if not self.registers:
            return "Empty _PCT"
        reg_type = ("Control", "Status")
        lines = ["{:<8s} {:<5s} {:<10s} {:<9s} {:<8s} {:<8s}".format("", "Type", "VendorCode", "ClassCode", "Arg0", "Arg1")]
        for reg_num, register in enumerate(self.registers):
            lines.append("{reg_num:<8s} {r.AddressSpaceId:<#5x} {r.BitWidth:<#10x} {r.BitOffset:<#9x} {r.AccessSize:<#8x} {r.Address:<#8x}".format(reg_num=reg_type[reg_num], r=register))
        return "\n".join(lines)

class _PDL(unpack.Struct):
    """P-State Depth Limit"""
    def __init__(self, pdl_data):
        super(_PDL, self).__init__()
        self.add_field('pstate_depth_limit', pdl_data)

    def __repr__(self):
        if self.pstate_depth_limit is None:
            return "Empty _PDL"
        return "Lowest performance state that OSPM can use = {}".format(self.pstate_depth_limit)

class _PPC(unpack.Struct):
    """Performance Present Capabilities"""
    def __init__(self, ppc_data):
        super(_PPC, self).__init__()
        self.add_field('highest_pstate', ppc_data)

    def __repr__(self):
        if self.highest_pstate is None:
            return "Empty _PPC"
        return "Highest performance state that OSPM can use = {}".format(self.highest_pstate)

class PSDDependency(unpack.Struct):
    def __init__(self, psd_data):
        super(PSDDependency, self).__init__()
        num_entries, revision, domain, coordination_type, num_processors = psd_data
        self.add_field('num_entries', num_entries)
        self.add_field('revision', revision)
        self.add_field('domain', domain)
        self.add_field('coordination_type', coordination_type)
        self.add_field('num_processors', num_processors)

class _PSD(unpack.Struct):
    """P-State Dependency"""
    def __init__(self, psd_data):
        super(_PSD, self).__init__()
        self.add_field('dependencies', tuple(map(PSDDependency, psd_data)))

    def __repr__(self):
        if not self.dependencies:
            return "Empty _PSD"
        lines = ["               {:<10s} {:<10s} {:<10s} {:<10s} {:<10s}".format("NumEntries", "Revision", "Dom", "CoordType", "NumProcessors")]
        for psd_num, dependency in enumerate(self.dependencies):
            lines.append("Dependency[{psd_num}]: {p.num_entries:<#10x} {p.revision:<#10x} {p.domain:<#10x} {p.coordination_type:<#10x} {p.num_processors:<#8x}".format(psd_num=psd_num, p=dependency))
        return "\n".join(lines)

class _PSS(unpack.Struct):
    """Performance Supported States"""
    def __init__(self, pss_data):
        super(_PSS, self).__init__()
        self.add_field('pstates', tuple(map(PState, pss_data)))

    def __repr__(self):
        if not self.pstates:
            return "Empty _PSS"
        lines = ["    {:10s} {:10s} {:12s} {:14s} {:10s} {:8s}".format("Freq (MHz)", "Power (mW)", "Latency (us)", "BMLatency (us)", "Control", "Status")]
        for pstate_num, pstate in enumerate(self.pstates):
            lines.append("P{pstate_num:<2d} {p.core_frequency:<10d} {p.power:<10d} {p.latency:<12d} {p.bus_master_latency:<14d} {p.control:#010x} {p.status:#010x}".format(pstate_num=pstate_num, p=pstate))
        return "\n".join(lines)

class PState(unpack.Struct):
    def __init__(self, pstate_data):
        super(PState, self).__init__()
        core_frequency, power, latency, bus_master_latency, control, status = pstate_data
        self.add_field('core_frequency', core_frequency, "{} MHz")
        self.add_field('power', power, "{} mW")
        self.add_field('latency', latency, "{} us")
        self.add_field('bus_master_latency', bus_master_latency, "{} us")
        self.add_field('control', control)
        self.add_field('status', status)

class _PTC(unpack.Struct):
    """Processor Throttling Control"""
    def __init__(self, ptc_data):
        super(_PTC, self).__init__()
        self.add_field('registers', tuple(map(GenericRegister, ptc_data)))

    def __repr__(self):
        if not self.registers:
            return "Empty _PTC"
        reg_type = ("Control", "Status")
        lines = ["{:<12s} {:<13s} {:<12s} {:<11s} {:<12s} {:s}".format("Processor", "AddrSpaceId", "BitWidth", "BitOffset", "AccessSize", "Address")]
        lines.append("{:<12s} {:<13s} {:<12s} {:<11s} {:<12s} {:s}".format("Throttling", "Type", "VendorCode", "ClassCode", "Arg0", "Arg1"))
        for reg_num, register in enumerate(self.registers):
            lines.append("{reg_num:<12s} {r.AddressSpaceId:<#13x} {r.BitWidth:<#12x} {r.BitOffset:<#11x} {r.AccessSize:<#12x} {r.Address:<#8x}".format(reg_num=reg_type[reg_num], r=register))
        return "\n".join(lines)

class _TDL(unpack.Struct):
    """T-State Depth Limit"""
    def __init__(self, tdl_data):
        super(_TDL, self).__init__()
        self.add_field('lowest_tstate', tdl_data)

    def __repr__(self):
        if self.lowest_tstate is None:
            return "Empty _TDL"
        return "Lowest throttling state that OSPM can use = {}".format(self.lowest_tstate)

class _TPC(unpack.Struct):
    """Throttling Present Capabilities"""
    def __init__(self, tpc_data):
        super(_TPC, self).__init__()
        self.add_field('highest_tstate', tpc_data)

    def __repr__(self):
        if self.highest_tstate is None:
            return "Empty _TPC"
        return "Highest throttling state that OSPM can use = {}".format(self.highest_tstate)

class _TSD(unpack.Struct):
    """T-State Dependency"""
    def __init__(self, tsd_data):
        super(_TSD, self).__init__()
        self.add_field('dependencies', tuple(map(TSDDependency, tsd_data)))

    def __repr__(self):
        if not self.dependencies:
            return "Empty _TSD"
        lines = ["                 {:<10s} {:<10s} {:<10s} {:<10s} {:<10s}".format("NumEntries", "Revision", "Dom", "CoordType", "NumProcessors")]
        for tsd_num, dependency in enumerate(self.dependencies):
            lines.append("Dependency[{tsd_num}]: {t.num_entries:<#10x} {t.revision:<#10x} {t.domain:<#10x} {t.coordination_type:<#10x} {t.num_processors:<#8x}".format(tsd_num=tsd_num, t=dependency))
        return "\n".join(lines)

class TSDDependency(unpack.Struct):
    def __init__(self, tsd_data):
        super(TSDDependency, self).__init__()
        num_entries, revision, domain, coordination_type, num_processors = tsd_data
        self.add_field('num_entries', num_entries)
        self.add_field('revision', revision)
        self.add_field('domain', domain)
        self.add_field('coordination_type', coordination_type)
        self.add_field('num_processors', num_processors)

class _TSS(unpack.Struct):
    """Throttling Supported States"""
    def __init__(self, tss_data):
        super(_TSS, self).__init__()
        self.add_field('tstates', tuple(map(TState, tss_data)))

    def __repr__(self):
        if not self.tstates:
            return "Empty _TSS"
        lines = ["    {:10s} {:10s} {:12s} {:10s} {:8s}".format("Freq (MHz)", "Power (mW)", "Latency (us)", "Control", "Status")]
        for tstate_num, tstate in enumerate(self.tstates):
            lines.append("T{tstate_num:<2d} {t.percent:<10d} {t.power:<10d} {t.latency:<12d} {t.control:#010x} {t.status:#010x}".format(tstate_num=tstate_num, t=tstate))
        return "\n".join(lines)

class TState(unpack.Struct):
    def __init__(self, tstate_data):
        super(TState, self).__init__()
        percent, power, latency, control, status = tstate_data
        self.add_field('percent', percent, "{}")
        self.add_field('power', power, "{} mW")
        self.add_field('latency', latency, "{} us")
        self.add_field('control', control)
        self.add_field('status', status)

def display_uid():
    """Find and display _UID"""
    def print_uid(uid):
        print "_UID = %s" % uid
    display_acpi_method("_UID", print_uid)

def get_table(signature, instance=1):
    """Get the requested ACPI table based on signature"""
    special_get = {
        'RSDP': _acpi._get_rsdp,
        'RSD PTR': _acpi._get_rsdp,
        'RSD PTR ': _acpi._get_rsdp,
        'RSDT': _acpi._get_rsdt,
        'XSDT': _acpi._get_xsdt,
    }.get(signature)
    if special_get is not None:
        if instance == 1:
            return special_get()
        return None
    return _acpi._get_table(signature, instance)

def get_table_list():
    """Get the list of ACPI table signatures"""
    tables = itertools.takewhile(lambda table: table is not None,
                                 (get_table_by_index(index) for index in itertools.count()))
    signatures = [table[:4] for table in tables]
    signatures.extend([s for s in ['RSDP', 'RSDT', 'XSDT'] if get_table(s)])
    signatures = sorted(set(signatures))
    return signatures

def display_objects(name="\\"):
    s = ""
    for path in get_objpaths(name):
        s += "{} ({})\n".format(path, acpi_object_types.get(get_object_info(path).object_type, "Reserved"))
    ttypager.ttypager_wrap(s, indent=False)

def dump(name=""):
    s = ''
    for path in get_objpaths(name):
        s += ttypager._wrap('{} : {!r}'.format(path, evaluate(path))) + '\n'
    return s

def dumptable(name="", instance=1):
    """Dump hexadecimal and printable ASCII bytes for an ACPI table specified by 4CC and instance"""
    s = ''
    data = get_table(name, instance)
    if data is None:
        s += "ACPI table with signature of {} and instance of {} not found.\n".format(name, instance)
        return s
    s += bits.dumpmem(data)
    return s

def dumptables():
    """Dump hexdecimal and printable ASCII bytes for all ACPI tables"""
    s = ''
    for signature in get_table_list():
        for instance in itertools.count(1,):
            data = get_table(signature, instance)
            if data is None:
                break
            s += "ACPI Table {} instance {}\n".format(signature, instance)
            s += bits.dumpmem(data)
    return s

created_explore_acpi_tables_cfg = False

def create_explore_acpi_tables_cfg():
    global created_explore_acpi_tables_cfg
    if created_explore_acpi_tables_cfg:
        return
    cfg = ""
    for signature in get_table_list():
        for instance in itertools.count(1):
            data = get_table(signature, instance)
            if data is None:
                break
            parse_method = 'parse_{}'.format(str.lower(signature), instance)
            if parse_method in globals():
                cfg += 'menuentry "Decode {} Instance {}" {{\n'.format(signature, instance)
                cfg += '    py "import acpi ; acpi.{}(printflag=True, instance={})"\n'.format(parse_method, instance)
                cfg += '}\n'
            if signature in ("APIC", "SRAT"):
                cfg += 'menuentry "Decode {} Instance {} (enabled only)" {{\n'.format(signature, instance)
                cfg += '    py "import acpi ; acpi.{}(EnabledOnly=True, instance={})"\n'.format(parse_method, instance)
                cfg += '}\n'
            cfg += 'menuentry "Dump {} Instance {} raw" {{\n'.format(signature, instance)
            cfg += """    py 'import ttypager, acpi; ttypager.ttypager(acpi.dumptable("{}", {}))'\n""".format(signature, instance)
            cfg += '}\n'
    bits.pyfs.add_static("explore_acpi_tables.cfg", cfg)
    created_explore_acpi_tables_cfg = True

created_explore_acpi_cpu_methods_cfg = False

def create_explore_acpi_cpu_methods_cfg():
    global created_explore_acpi_cpu_methods_cfg
    if created_explore_acpi_cpu_methods_cfg:
        return
    methods = set()
    for c in get_cpupaths():
        for o in get_objpaths(c + "."):
            method = o[len(c)+1:]
            if "." in method:
                continue
            methods.add(method)
    cfg = ""
    for method in sorted(methods):
        # Whitelist for now until splitting this into its own module
        if method in ("_MAT", "_CSD", "_CST", "_PCT", "PDL", "_PPC", "_PTC", "_PSD", "_PSS", "_TDL", "_TPC", "_TSD", "_TSS"):
            cfg += 'menuentry "{} ({})" {{\n'.format(method, globals()[method].__doc__)
            cfg += """    py 'import acpi ; acpi.display_cpu_method("{}")'\n""".format(method)
            cfg += '}\n'
    bits.pyfs.add_static("explore_acpi_cpu_methods.cfg", cfg)
    created_explore_acpi_cpu_methods_cfg = True

def show_checksum(signature, instance=1):
    """Compute checksum of ACPI table"""

    data = get_table(signature, instance)
    if data is None:
        print "ACPI table with signature of {} and instance of {} not found.\n".format(signature, instance)
        return

    csum = sum(ord(c) for c in data)
    print 'Full checksum is {:#x}'.format(csum)
    print '1-byte checksum is {:#x}'.format(csum & 0xff)

def efi_save_tables():
    """Save all ACPI tables to files; only works under EFI.

    Warning: All files in the /acpi directory will be deleted!"""

    import efi

    root = efi.get_boot_fs()
    acpidir = root.mkdir("acpi")
    # delete all files in \acpi directory
    for f in os.listdir("/acpi"):
        acpidir.open(f, efi.EFI_FILE_MODE_READ | efi.EFI_FILE_MODE_WRITE).delete()

    for signature in get_table_list():
        for instance in itertools.count(1):
            data = get_table(signature, instance)
            if data is None:
                break
            basename = signature
            if instance > 1:
                basename += "{}".format(instance)
            acpidir.create("{}.bin".format(basename)).write(data)
            if signature in ("FACP", "RSDP", "RSDT", "XSDT"):
                data = repr(parse_table(signature, instance))
                if signature in ("RSDP"):
                    data = "RSDP address = {:#x}\n{}".format(_acpi._get_root_pointer(), data)
                acpidir.create("{}.txt".format(basename)).write(data)
