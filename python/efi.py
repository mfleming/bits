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

"""efi module."""

import _efi
import binascii
import bits
import ctypes, _ctypes
import redirect
import os
import uuid

known_uuids = {
    uuid.UUID('05ad34ba-6f02-4214-952e-4da0398e2bb9'): 'EFI_DXE_SERVICES_TABLE_GUID',
    uuid.UUID('09576e91-6d3f-11d2-8e39-00a0c969723b'): 'EFI_DEVICE_PATH_PROTOCOL_GUID',
    uuid.UUID('09576e92-6d3f-11d2-8e39-00a0c969723b'): 'EFI_FILE_INFO_ID',
    uuid.UUID('09576e93-6d3f-11d2-8e39-00a0c969723b'): 'EFI_FILE_SYSTEM_INFO_ID',
    uuid.UUID('107a772c-d5e1-11d4-9a46-0090273fc14d'): 'EFI_COMPONENT_NAME_PROTOCOL_GUID',
    uuid.UUID('11b34006-d85b-4d0a-a290-d5a571310ef7'): 'PCD_PROTOCOL_GUID',
    uuid.UUID('13a3f0f6-264a-3ef0-f2e0-dec512342f34'): 'EFI_PCD_PROTOCOL_GUID',
    uuid.UUID('13ac6dd1-73d0-11d4-b06b-00aa00bd6de7'): 'EFI_EBC_INTERPRETER_PROTOCOL_GUID',
    uuid.UUID('18a031ab-b443-4d1a-a5c0-0c09261e9f71'): 'EFI_DRIVER_BINDING_PROTOCOL_GUID',
    uuid.UUID('2755590c-6f3c-42fa-9ea4-a3ba543cda25'): 'EFI_DEBUG_SUPPORT_PROTOCOL_GUID',
    uuid.UUID('2f707ebb-4a1a-11d4-9a38-0090273FC14D'): 'EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GUID',
    uuid.UUID('31878c87-0b75-11d5-9a4f-0090273fc14d'): 'EFI_SIMPLE_POINTER_PROTOCOL_GUID',
    uuid.UUID('31a6406a-6bdf-4e46-b2a2-ebaa89c40920'): 'EFI_HII_IMAGE_PROTOCOL_GUID',
    uuid.UUID('330d4706-f2a0-4e4f-a369-b66fa8d54385'): 'EFI_HII_CONFIG_ACCESS_PROTOCOL_GUID',
    uuid.UUID('387477c1-69c7-11d2-8e39-00a0c969723b'): 'EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID',
    uuid.UUID('387477c2-69c7-11d2-8e39-00a0c969723b'): 'EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID',
    uuid.UUID('39b68c46-f7fb-441b-b6ec-16b0f69821f3'): 'EFI_CAPSULE_REPORT_GUID',
    uuid.UUID('49152e77-1ada-4764-b7a2-7afefed95e8b'): 'EFI_DEBUG_IMAGE_INFO_TABLE_GUID',
    uuid.UUID('4c19049f-4137-4dd3-9c10-8b97a83ffdfa'): 'EFI_MEMORY_TYPE_INFORMATION_GUID',
    uuid.UUID('4cf5b200-68b8-4ca5-9eec-b23e3f50029a'): 'EFI_PCI_IO_PROTOCOL_GUID',
    uuid.UUID('4d330321-025f-4aac-90d8-5ed900173b63'): 'EFI_DRIVER_DIAGNOSTICS_PROTOCOL_GUID',
    uuid.UUID('587e72d7-cc50-4f79-8209-ca291fc1a10f'): 'EFI_HII_CONFIG_ROUTING_PROTOCOL_GUID',
    uuid.UUID('59324945-ec44-4c0d-b1cd-9db139df070c'): 'EFI_ISCSI_INITIATOR_NAME_PROTOCOL_GUID',
    uuid.UUID('5b1b31a1-9562-11d2-8e3f-00a0c969723b'): 'EFI_LOADED_IMAGE_PROTOCOL_GUID',
    uuid.UUID('6a1ee763-d47a-43b4-aabe-ef1de2ab56fc'): 'EFI_HII_PACKAGE_LIST_PROTOCOL_GUID',
    uuid.UUID('6a7a5cff-e8d9-4f70-bada-75ab3025ce14'): 'EFI_COMPONENT_NAME2_PROTOCOL_GUID',
    uuid.UUID('7739f24c-93d7-11d4-9a3a-0090273fc14d'): 'EFI_HOB_LIST_GUID',
    uuid.UUID('783658a3-4172-4421-a299-e009079c0cb4'): 'EFI_LEGACY_BIOS_PLATFORM_PROTOCOL_GUID',
    uuid.UUID('8868e871-e4f1-11d3-bc22-0080c73c8881'): 'EFI_ACPI_TABLE_GUID',
    uuid.UUID('8b843e20-8132-4852-90cc-551a4e4a7f1c'): 'EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID',
    uuid.UUID('8d59d32b-c655-4ae9-9b15-f25904992a43'): 'EFI_ABSOLUTE_POINTER_PROTOCOL_GUID',
    uuid.UUID('9042a9de-23dc-4a38-96fb-7aded080516a'): 'EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID',
    uuid.UUID('964e5b21-6459-11d2-8e39-00a0c969723b'): 'EFI_BLOCK_IO_PROTOCOL_GUID',
    uuid.UUID('964e5b22-6459-11d2-8e39-00a0c969723b'): 'EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID',
    uuid.UUID('a4c751fc-23ae-4c3e-92e9-4964cf63f349'): 'EFI_UNICODE_COLLATION_PROTOCOL2_GUID',
    uuid.UUID('b9d4c360-bcfb-4f9b-9298-53c136982258'): 'EFI_FORM_BROWSER2_PROTOCOL_GUID',
    uuid.UUID('bb25cf6f-f1d4-11d2-9a0c-0090273fc1fd'): 'EFI_SERIAL_IO_PROTOCOL_GUID',
    uuid.UUID('bc62157e-3e33-4fec-9920-2d3b36d750df'): 'EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID',
    uuid.UUID('ce345171-ba0b-11d2-8e4f-00a0c969723b'): 'EFI_DISK_IO_PROTOCOL_GUID',
    uuid.UUID('d42ae6bd-1352-4bfb-909a-ca72a6eae889'): 'LZMAF86_CUSTOM_DECOMPRESS_GUID',
    uuid.UUID('d8117cfe-94a6-11d4-9a3a-0090273fc14d'): 'EFI_DECOMPRESS_PROTOCOL',
    uuid.UUID('db47d7d3-fe81-11d3-9a35-0090273fc14d'): 'EFI_FILE_SYSTEM_VOLUME_LABEL_ID',
    uuid.UUID('dd9e7534-7762-4698-8c14-f58517a625aa'): 'EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL',
    uuid.UUID('e9ca4775-8657-47fc-97e7-7ed65a084324'): 'EFI_HII_FONT_PROTOCOL_GUID',
    uuid.UUID('eb9d2d2f-2d88-11d3-9a16-0090273fc14d'): 'MPS_TABLE_GUID',
    uuid.UUID('eb9d2d30-2d88-11d3-9a16-0090273fc14d'): 'ACPI_TABLE_GUID',
    uuid.UUID('eb9d2d31-2d88-11d3-9a16-0090273fc14d'): 'SMBIOS_TABLE_GUID',
    uuid.UUID('eb9d2d32-2d88-11d3-9a16-0090273fc14d'): 'SAL_SYSTEM_TABLE_GUID',
    uuid.UUID('eba4e8d2-3858-41ec-a281-2647ba9660d0'): 'EFI_DEBUGPORT_PROTOCOL_GUID',
    uuid.UUID('ee4e5898-3914-4259-9d6e-dc7bd79403cf'): 'LZMA_CUSTOM_DECOMPRESS_GUID',
    uuid.UUID('ef9fc172-a1b2-4693-b327-6d32fc416042'): 'EFI_HII_DATABASE_PROTOCOL_GUID',
    uuid.UUID('f4ccbfb7-f6e0-47fd-9dd4-10a8f150c191'): 'EFI_SMM_BASE2_PROTOCOL_GUID',
    uuid.UUID('f541796d-a62e-4954-a775-9584f61b9cdd'): 'EFI_TCG_PROTOCOL_GUID',
    uuid.UUID('fc1bcdb0-7d31-49aa-936a-a4600d9dd083'): 'EFI_CRC32_GUIDED_SECTION_EXTRACTION_GUID',
    uuid.UUID('ffe06bdd-6107-46a6-7bb2-5a9c7ec5275c'): 'EFI_ACPI_TABLE_PROTOCOL_GUID',
}

# Create each of the values above as a constant referring to the corresponding UUID.
globals().update(map(reversed, known_uuids.iteritems()))

def to_bytes(var):
    return (ctypes.c_char * ctypes.sizeof(var)).from_buffer(var).raw

_CTYPES_HEX_TYPES = (
    ctypes.c_void_p,
    ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint32, ctypes.c_uint64,
    ctypes.c_ubyte, ctypes.c_ushort, ctypes.c_uint, ctypes.c_ulong, ctypes.c_ulonglong,
)

class Struct(ctypes.Structure):
    """Base class for ctypes structures."""
    @staticmethod
    def _formatval(t, val):
        if val is not None and t in _CTYPES_HEX_TYPES:
            return "{:#x}".format(val)
        if issubclass(t, _ctypes.Array) and not issubclass(t._type_, (ctypes.c_char, ctypes.c_wchar)):
            return "[{}]".format(", ".join(Struct._formatval(t._type_, item) for item in val))
        return "{}".format(val)

    def __str__(self):
        return "{}({})".format(self.__class__.__name__, ", ".join("{}={}".format(n, self._formatval(t, getattr(self, n))) for n,t in self._fields_))

class Protocol(Struct):
    """Base class for EFI protocols. Derived classes must have a uuid.UUID named guid."""
    @classmethod
    def from_handle(cls, handle):
        """Retrieve an instance of this protocol from an EFI handle"""
        return cls.from_address(get_protocol(handle, cls.guid))

ptrsize = ctypes.sizeof(ctypes.c_void_p)

def call(func, *args):
    """Call an EFI function."""
    return getattr(_efi, "_call{}".format(len(args)))(func, *args)

if ptrsize == 4:
    class split64(object):
        def __init__(self, value):
            self.value = value
    def args64compat(*args):
        """Fix an EFI function's argument list to handle 64-bit arguments on 32-bit platforms.

        Wrap each 64-bit argument with split64 before calling this function."""
        for arg in args:
            if isinstance(arg, split64):
                yield arg.value & 0xFFFFFFFF
                yield arg.value >> 32
            else:
                yield arg
    call32 = call
    def call(func, *args):
        """Call an EFI function.

        For EFI functions that take 64-bit arguments, wrap them with split64
        before calling this function."""
        return call32(func, *args64compat(*args))
else:
    def split64(arg):
        return arg

def compute_crc(buf, offset):
    before_buffer = (ctypes.c_uint8 * offset).from_buffer(buf)
    zero = (ctypes.c_uint8 * 4)()
    after_buffer = (ctypes.c_uint8 * (ctypes.sizeof(buf) - offset - 4)).from_buffer(buf, offset + 4)
    crc = binascii.crc32(before_buffer)
    crc = binascii.crc32(zero, crc)
    return binascii.crc32(after_buffer, crc)

def table_crc(addr):
    th = TableHeader.from_address(addr)
    buf = (ctypes.c_uint8 * th.HeaderSize).from_address(addr)
    crc = compute_crc(buf, TableHeader.CRC32.offset)
    return th.CRC32 == ctypes.c_uint32(crc).value

class DevicePathProtocol(Protocol):
    guid = EFI_DEVICE_PATH_PROTOCOL_GUID
    _fields_ = [
        ("Type", ctypes.c_uint8),
        ("SubType", ctypes.c_uint8),
        ("Length", ctypes.c_uint8 * 2),
    ]

class DevicePathToTextProtocol(Protocol):
    guid = EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID
    _fields_ = [
        ("ConvertDeviceNodeToText", ctypes.c_void_p),
        ("ConvertDevicePathToText", ctypes.c_void_p),
    ]

    def _helper(self, method, path):
        ucs2_string_ptr = check_error_value(call(method, ctypes.addressof(path), 0, 0))
        try:
            s = ctypes.wstring_at(ucs2_string_ptr)
        finally:
            check_status(call(system_table.BootServices.contents.FreePool, ucs2_string_ptr))
        return s

    def device_path_text(self, path):
        """Convert the specified device path to text."""
        return self._helper(self.ConvertDevicePathToText, path)

    def device_node_text(self, path):
        """Convert the specified device node to text."""
        return self._helper(self.ConvertDeviceNodeToText, path)

class SimpleTextInputProtocol(Protocol):
    """EFI Simple Text Input Protocol"""
    guid = EFI_SIMPLE_TEXT_INPUT_PROTOCOL_GUID
    _fields_ = [
        ('Reset', ctypes.c_void_p),
        ('ReadKeyStroke', ctypes.c_void_p),
        ('WaitForKey', ctypes.c_void_p),
    ]

class SimpleTextOutputProtocol(Protocol):
    """EFI Simple Text Output Protocol"""
    guid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID
    _fields_ = [
        ('Reset', ctypes.c_void_p),
        ('OutputString', ctypes.c_void_p),
        ('TestString', ctypes.c_void_p),
        ('QueryMode', ctypes.c_void_p),
        ('SetMode', ctypes.c_void_p),
        ('SetAttribute', ctypes.c_void_p),
        ('ClearScreen', ctypes.c_void_p),
        ('SetCursorPosition', ctypes.c_void_p),
        ('EnableCursor', ctypes.c_void_p),
        ('Mode', ctypes.c_void_p),
    ]

class GUID(Struct):
    _fields_ = [
        ('Data', ctypes.c_ubyte * 16),
    ]

    def __init__(self, *args, **kwargs):
        """Create a GUID.  Accepts any arguments the uuid.UUID constructor
        would accept.  Also accepts an instance of uuid.UUID, either as the
        first argument or as a keyword argument "uuid".  As with other
        ctypes structures, passing no parameters yields a zero-initialized
        structure."""
        u = kwargs.get("uuid")
        if u is not None:
            self.uuid = u
        elif not(args) and not(kwargs):
            self.uuid = uuid.UUID(int=0)
        elif args and isinstance(args[0], uuid.UUID):
            self.uuid = args[0]
        else:
            self.uuid = uuid.UUID(*args, **kwargs)

    def _get_uuid(self):
        return uuid.UUID(bytes_le=to_bytes(self))

    def _set_uuid(self, u):
        ctypes.memmove(ctypes.addressof(self), ctypes.c_char_p(u.bytes_le), ctypes.sizeof(self))

    uuid = property(_get_uuid, _set_uuid)

    def __cmp__(self, other):
        if isinstance(other, GUID):
            return cmp(self.uuid, other.uuid)
        if isinstance(other, uuid.UUID):
            return cmp(self.uuid, other)
        return NotImplemented

    def __hash__(self):
        return hash(self.uuid)

    def __repr__(self):
        return "GUID({})".format(self.uuid)

    def __str__(self):
        return "{}".format(self.uuid)

class ConfigurationTable(Struct):
    """Decode the EFI Configuration Table"""
    _fields_ = [
        ('VendorGuid', GUID),
        ('VendorTable', ctypes.c_void_p),
    ]

class TableHeader(Struct):
    """Decode the EFI Table Header"""
    _fields_ = [
        ('Signature', ctypes.c_uint64),
        ('Revision', ctypes.c_uint32),
        ('HeaderSize', ctypes.c_uint32),
        ('CRC32', ctypes.c_uint32),
        ('Reserved', ctypes.c_uint32),
    ]

class BootServices(Struct):
    """Decode the EFI Boot Services"""
    _fields_ = [
        ('Hdr', TableHeader),
        ('RaiseTPL', ctypes.c_void_p),
        ('RestoreTPL', ctypes.c_void_p),
        ('AllocatePages', ctypes.c_void_p),
        ('FreePages',  ctypes.c_void_p),
        ('GetMemoryMap', ctypes.c_void_p),
        ('AllocatePool', ctypes.c_void_p),
        ('FreePool', ctypes.c_void_p),
        ('CreateEvent', ctypes.c_void_p),
        ('SetTimer', ctypes.c_void_p),
        ('WaitForEvent', ctypes.c_void_p),
        ('SignalEvent', ctypes.c_void_p),
        ('CloseEvent', ctypes.c_void_p),
        ('CheckEvent', ctypes.c_void_p),
        ('InstallProtocolInterface',  ctypes.c_void_p),
        ('ReinstallProtocolInterface', ctypes.c_void_p),
        ('UninstallProtocolInterface', ctypes.c_void_p),
        ('HandleProtocol', ctypes.c_void_p),
        ('Reserved', ctypes.c_void_p),
        ('RegisterProtocolNotify', ctypes.c_void_p),
        ('LocateHandle',  ctypes.c_void_p),
        ('LocateDevicePath',  ctypes.c_void_p),
        ('InstallConfigurationTable', ctypes.c_void_p),
        ('LoadImage', ctypes.c_void_p),
        ('StartImage',  ctypes.c_void_p),
        ('Exit', ctypes.c_void_p),
        ('UnloadImage', ctypes.c_void_p),
        ('ExitBootServices', ctypes.c_void_p),
        ('GetNextMonotonicCount', ctypes.c_void_p),
        ('Stall', ctypes.c_void_p),
        ('SetWatchdogTimer', ctypes.c_void_p),
        ('ConnectController',  ctypes.c_void_p),
        ('DisconnectController', ctypes.c_void_p),
        ('OpenProtocol', ctypes.c_void_p),
        ('CloseProtocol', ctypes.c_void_p),
        ('OpenProtocolInformation', ctypes.c_void_p),
        ('ProtocolsPerHandle', ctypes.c_void_p),
        ('LocateHandleBuffer', ctypes.c_void_p),
        ('LocateProtocol', ctypes.c_void_p),
        ('InstallMultipleProtocolInterfaces', ctypes.c_void_p),
        ('UninstallMultipleProtocolInterfaces', ctypes.c_void_p),
        ('CalculateCrc32', ctypes.c_void_p),
        ('CopyMem', ctypes.c_void_p),
        ('SetMem', ctypes.c_void_p),
        ('CreateEventEx',  ctypes.c_void_p),
    ]

class RuntimeServices(Struct):
    """Decode the EFI Runtime Services"""
    _fields_ = [
        ('Hdr', TableHeader),
        ('GetTime', ctypes.c_void_p),
        ('SetTime', ctypes.c_void_p),
        ('GetWakeupTime', ctypes.c_void_p),
        ('SetWakeupTime', ctypes.c_void_p),
        ('SetVirtualAddressMap', ctypes.c_void_p),
        ('ConvertPointer', ctypes.c_void_p),
        ('GetVariable', ctypes.c_void_p),
        ('GetNextVariableName', ctypes.c_void_p),
        ('SetVariable', ctypes.c_void_p),
        ('GetNextHighMonotonicCount', ctypes.c_void_p),
        ('ResetSystem', ctypes.c_void_p),
        ('UpdateCapsule', ctypes.c_void_p),
        ('QueryCapsuleCapabilities', ctypes.c_void_p),
        ('QueryVariableInfo', ctypes.c_void_p),
    ]

class SystemTable(Struct):
    """Decode the EFI System Table."""
    _fields_ = [
        ('Hdr', TableHeader),
        ('FirmwareVendor', ctypes.c_wchar_p),
        ('FirmwareRevision', ctypes.c_uint32),
        ('ConsoleInHandle', ctypes.c_ulong),
        ('ConIn', ctypes.POINTER(SimpleTextInputProtocol)),
        ('ConsoleOutHandle', ctypes.c_ulong),
        ('ConOut', ctypes.POINTER(SimpleTextOutputProtocol)),
        ('StandardErrorHandle', ctypes.c_ulong),
        ('StdErr', ctypes.POINTER(SimpleTextOutputProtocol)),
        ('RuntimeServices', ctypes.POINTER(RuntimeServices)),
        ('BootServices', ctypes.POINTER(BootServices)),
        ('NumberOfTableEntries', ctypes.c_ulong),
        ('ConfigurationTablePtr', ctypes.POINTER(ConfigurationTable)),
    ]

    @property
    def ConfigurationTable(self):
        ptr = ctypes.cast(self.ConfigurationTablePtr, ctypes.c_void_p)
        return (ConfigurationTable * self.NumberOfTableEntries).from_address(ptr.value)

    @property
    def ConfigurationTableDict(self):
        return dict((t.VendorGuid, t.VendorTable) for t in self.ConfigurationTable)

system_table = SystemTable.from_address(_efi._system_table)

class LoadedImageProtocol(Protocol):
    """EFI Loaded Image Protocol"""
    guid = EFI_LOADED_IMAGE_PROTOCOL_GUID
    _fields_ = [
        ('Revision', ctypes.c_uint32),
        ('ParentHandle', ctypes.c_void_p),
        ('SystemTable', ctypes.c_void_p),
        ('DeviceHandle', ctypes.c_void_p),
        ('FilePath', ctypes.POINTER(DevicePathProtocol)),
        ('Reserved', ctypes.c_void_p),
        ('LoadOptionsSize', ctypes.c_uint32),
        ('LoadOptions', ctypes.c_void_p),
        ('ImageBase', ctypes.c_void_p),
        ('ImageSize', ctypes.c_uint64),
        ('ImageCodeType', ctypes.c_uint32),
        ('ImageDataType', ctypes.c_uint32),
        ('Unload', ctypes.c_void_p),
    ]

class SimpleFileSystemProtocol(Protocol):
    """EFI Simple File System Protocol"""
    guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID
    _fields_ = [
        ('Revision', ctypes.c_uint64),
        ('OpenVolume', ctypes.c_void_p),
    ]

    @property
    def root(self):
        root_ptr = ctypes.POINTER(FileProtocol)()
        check_status(call(self.OpenVolume, ctypes.addressof(self), ctypes.addressof(root_ptr)))
        return efi_file(root_ptr.contents)

class FileProtocol(Struct):
    """EFI File Protocol"""
    _fields_ = [
        ('Revision', ctypes.c_uint64),
        ('Open', ctypes.c_void_p),
        ('Close', ctypes.c_void_p),
        ('Delete', ctypes.c_void_p),
        ('Read', ctypes.c_void_p),
        ('Write', ctypes.c_void_p),
        ('GetPosition', ctypes.c_void_p),
        ('SetPosition', ctypes.c_void_p),
        ('GetInfo', ctypes.c_void_p),
        ('SetInfo', ctypes.c_void_p),
        ('Flush', ctypes.c_void_p),
    ]

class Time(Struct):
    """EFI Time"""
    _fields_ = [
        ('Year', ctypes.c_uint16),
        ('Month', ctypes.c_uint8),
        ('Day', ctypes.c_uint8),
        ('Hour', ctypes.c_uint8),
        ('Minute', ctypes.c_uint8),
        ('Second', ctypes.c_uint8),
        ('Pad1', ctypes.c_uint8),
        ('Nanosecond', ctypes.c_uint32),
        ('TimeZone', ctypes.c_int16),
        ('Daylight', ctypes.c_uint8),
        ('Pad2', ctypes.c_uint8),
    ]

def make_UCS2_name_property():
    """Create a variable-sized UCS2-encoded name property at the end of the structure

    Automatically resizes the structure and updates the field named "Size"
    when set."""
    def _get_name(self):
        return ctypes.wstring_at(ctypes.addressof(self) + ctypes.sizeof(self.__class__))

    def _set_name(self, name):
        b = ctypes.create_unicode_buffer(name)
        ctypes.resize(self, ctypes.sizeof(self.__class__) + ctypes.sizeof(b))
        ctypes.memmove(ctypes.addressof(self) + ctypes.sizeof(self.__class__), ctypes.addressof(b), ctypes.sizeof(b))
        self.Size = ctypes.sizeof(b)

    return property(_get_name, _set_name)

class FileInfo(Struct):
    """EFI File Info"""
    _fields_ = [
        ('Size', ctypes.c_uint64),
        ('FileSize', ctypes.c_uint64),
        ('PhysicalSize', ctypes.c_uint64),
        ('CreateTime', Time),
        ('LastAccessTime', Time),
        ('ModificationTime', Time),
        ('Attribute', ctypes.c_uint64),
    ]

    FileName = make_UCS2_name_property()

class FileSystemInfo(Struct):
    """EFI File System Info"""
    _pack_ = 4
    _fields_ = [
        ('Size', ctypes.c_uint64),
        ('ReadOnly', ctypes.c_uint8),
        ('_pad', ctypes.c_uint8 * 7),
        ('VolumeSize', ctypes.c_uint64),
        ('FreeSpace', ctypes.c_uint64),
        ('BlockSize', ctypes.c_uint32),
    ]

    VolumeLabel = make_UCS2_name_property()

class efi_file(object):
    """A file-like object for an EFI file"""
    def __init__(self, file_protocol):
        self.file_protocol = file_protocol
        self.closed = False

    def _check_closed(self):
        if self.closed:
            raise ValueError("I/O operation on closed file")

    def __del__(self):
        self.close()

    def close(self):
        if not self.closed:
            check_status(call(self.file_protocol.Close, ctypes.addressof(self.file_protocol)))
            self.closed = True

    def delete(self):
        self._check_closed()
        try:
            check_status(call(self.file_protocol.Delete, ctypes.addressof(self.file_protocol)))
        finally:
            self.closed = True

    def flush(self):
        self._check_closed()
        check_status(call(self.file_protocol.Flush, ctypes.addressof(self.file_protocol)))

    def read(self, size=-1):
        self._check_closed()
        if size < 0:
            try:
                size = self.file_info.FileSize - self.tell()
            except EFIException as e:
                size = self.file_info.FileSize
        size = ctypes.c_ulong(size)
        buf = ctypes.create_string_buffer(0)
        ctypes.resize(buf, size.value)
        check_status(call(self.file_protocol.Read, ctypes.addressof(self.file_protocol), ctypes.addressof(size), ctypes.addressof(buf)))
        if size.value != ctypes.sizeof(buf):
            ctypes.resize(buf, size.value)
        return buf.raw

    def seek(self, offset, whence=0):
        self._check_closed()
        if whence == 0:
            pos = offset
        elif whence == 1:
            pos = self.tell() + offset
        elif whence == 2:
            pos = self.file_info.FileSize + offset
        else:
            raise ValueError("seek: whence makes no sense: {}".format(whence))
        check_status(call(self.file_protocol.SetPosition, ctypes.addressof(self.file_protocol), split64(pos)))

    def tell(self):
        self._check_closed()
        pos = ctypes.c_uint64()
        check_status(call(self.file_protocol.GetPosition, ctypes.addressof(self.file_protocol), ctypes.addressof(pos)))
        return pos.value

    def write(self, s):
        self._check_closed()
        buf = ctypes.create_string_buffer(s, len(s))
        size = ctypes.c_ulong(ctypes.sizeof(buf))
        check_status(call(self.file_protocol.Write, ctypes.addressof(self.file_protocol), ctypes.addressof(size), ctypes.addressof(buf)))

    def open(self, name, mode, attrib=0):
        self._check_closed()
        new_protocol = ctypes.POINTER(FileProtocol)()
        ucs2_name = ctypes.create_unicode_buffer(name)
        check_status(call(self.file_protocol.Open, ctypes.addressof(self.file_protocol), ctypes.addressof(new_protocol), ctypes.addressof(ucs2_name), split64(mode), split64(attrib)))
        return efi_file(new_protocol.contents)

    def create(self, name, attrib=0):
        """Create a file. Shorthand for open with read/write/create."""
        return self.open(name, EFI_FILE_MODE_CREATE | EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, attrib)

    def mkdir(self, name, attrib=0):
        """Make a directory. Shorthand for create EFI_FILE_DIRECTORY.

        attrib, if specified, provides additional attributes beyond EFI_FILE_DIRECTORY."""
        return self.create(name, EFI_FILE_DIRECTORY | attrib)

    def _get_info(self, information_type_guid, info):
        self._check_closed()
        guid = GUID(information_type_guid)
        size = ctypes.c_ulong()
        status = call(self.file_protocol.GetInfo, ctypes.addressof(self.file_protocol), ctypes.addressof(guid), ctypes.addressof(size), 0)
        if status != EFI_BUFFER_TOO_SMALL:
            check_status(status)
        ctypes.resize(info, size.value)
        check_status(call(self.file_protocol.GetInfo, ctypes.addressof(self.file_protocol), ctypes.addressof(guid), ctypes.addressof(size), ctypes.addressof(info)))
        return info

    def get_file_info(self):
        return self._get_info(EFI_FILE_INFO_ID, FileInfo())

    def get_file_system_info(self):
        return self._get_info(EFI_FILE_SYSTEM_INFO_ID, FileSystemInfo())

    def get_volume_label(self):
        return self._get_info(EFI_FILE_SYSTEM_VOLUME_LABEL_ID, ctypes.create_unicode_buffer(0)).value

    def _set_info(self, information_type_guid, info):
        self._check_closed()
        guid = GUID(information_type_guid)
        check_status(call(self.file_protocol.SetInfo, ctypes.addressof(self.file_protocol), ctypes.addressof(guid), ctypes.sizeof(info), ctypes.addressof(info)))

    def set_file_info(self, info):
        self._set_info(EFI_FILE_INFO_ID, info)

    def set_file_system_info(self, info):
        self._set_info(EFI_FILE_SYSTEM_INFO_ID, info)

    def set_volume_label(self, label):
        buf = ctypes.create_unicode_buffer(label)
        self._set_info(EFI_FILE_SYSTEM_VOLUME_LABEL_ID, buf)

    file_info = property(get_file_info, set_file_info)
    file_system_info = property(get_file_system_info, set_file_system_info)
    volume_label = property(get_volume_label, set_volume_label)

EFI_FILE_MODE_READ = 0x0000000000000001
EFI_FILE_MODE_WRITE = 0x0000000000000002
EFI_FILE_MODE_CREATE = 0x8000000000000000

EFI_FILE_READ_ONLY = 0x0000000000000001
EFI_FILE_HIDDEN = 0x0000000000000002
EFI_FILE_SYSTEM = 0x0000000000000004
EFI_FILE_RESERVED = 0x0000000000000008
EFI_FILE_DIRECTORY = 0x0000000000000010
EFI_FILE_ARCHIVE = 0x0000000000000020

EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL = 0x00000001
EFI_OPEN_PROTOCOL_GET_PROTOCOL = 0x00000002
EFI_OPEN_PROTOCOL_TEST_PROTOCOL = 0x00000004
EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER = 0x00000008
EFI_OPEN_PROTOCOL_BY_DRIVER = 0x00000010
EFI_OPEN_PROTOCOL_EXCLUSIVE = 0x00000020

def locate_handles(protocol_guid=None):
    """Locate handles supporting a given protocol, or all handles if protocol_guid is None"""
    if protocol_guid is not None:
        guid = GUID(protocol_guid)
        guid_addr = ctypes.addressof(guid)
        search_type = ByProtocol
    else:
        guid_addr = 0
        search_type = AllHandles
    size = ctypes.c_ulong(0)
    status = call(system_table.BootServices.contents.LocateHandle, search_type, guid_addr, 0, ctypes.addressof(size), 0)
    if status != EFI_BUFFER_TOO_SMALL:
        check_status(status)
    handles = (ctypes.c_void_p * (size.value / ctypes.sizeof(ctypes.c_void_p)))()
    check_status(call(system_table.BootServices.contents.LocateHandle, search_type, guid_addr, 0, ctypes.addressof(size), ctypes.addressof(handles)))
    return handles

def get_protocol(handle, protocol_guid):
    """Get the given protocol of the given handle

    Uses OpenProtocol with the BITS image handle, so CloseProtocol is
    optional."""
    guid = GUID(protocol_guid)
    protocol_addr = ctypes.c_void_p()
    check_status(call(system_table.BootServices.contents.OpenProtocol, handle, ctypes.addressof(guid), ctypes.addressof(protocol_addr), _efi._image_handle, 0, EFI_OPEN_PROTOCOL_GET_PROTOCOL))
    return protocol_addr.value

# EFI errors have the high bit set, so use the pointer size to find out how
# high your EFI is.
EFI_ERROR = 1 << (8*ptrsize - 1)

EFI_SUCCESS = 0
EFI_BUFFER_TOO_SMALL = EFI_ERROR | 5
EFI_NOT_FOUND = EFI_ERROR | 14

class EFIException(Exception):
    pass

def check_status(status):
    """Check an EFI status value, and raise an exception if not EFI_SUCCESS

    To check non-status values that may have the error bit set, use check_error_value instead."""
    if status != EFI_SUCCESS:
        raise EFIException(status)

def check_error_value(value):
    """Check a value that may have the error bit set

    Raises an exception if the error bit is set; otherwise, returns the value."""
    if value & EFI_ERROR:
        raise EFIException(value)
    return value

def loaded_image():
    return LoadedImageProtocol.from_handle(_efi._image_handle)

def get_boot_fs():
    return SimpleFileSystemProtocol.from_handle(loaded_image().DeviceHandle).root

def print_variables():
    name = ctypes.create_unicode_buffer("")
    size = ctypes.c_ulong(ctypes.sizeof(name))
    guid = GUID()
    while True:
        status = call(system_table.RuntimeServices.contents.GetNextVariableName, ctypes.addressof(size), ctypes.addressof(name), ctypes.addressof(guid))
        if status == EFI_NOT_FOUND:
            break
        if status == EFI_BUFFER_TOO_SMALL:
            ctypes.resize(name, size.value)
            continue
        check_status(status)
        print name.value, guid
        data, attributes, data_size = get_variable(name, guid)
        print "attributes={:#x} size={} data:".format(attributes, data_size)
        print bits.dumpmem(data.raw)

def get_variable(name, guid):
    attribute = ctypes.c_uint32(0)
    data = ctypes.create_string_buffer(1)
    size = ctypes.c_ulong(ctypes.sizeof(data))
    while True:
        status = call(system_table.RuntimeServices.contents.GetVariable, ctypes.addressof(name), ctypes.addressof(guid), ctypes.addressof(attribute), ctypes.addressof(size), ctypes.addressof(data))
        if status == EFI_NOT_FOUND:
            break
        if status == EFI_BUFFER_TOO_SMALL:
            ctypes.resize(data, size.value)
            continue
        check_status(status)
        return data, attribute.value, size.value

def log_efi_info():
    with redirect.logonly():
        try:
            print
            print "EFI system information:"
            print "Firmware Vendor:", system_table.FirmwareVendor
            print "Firmware Revision: {:#x}".format(system_table.FirmwareRevision)
            print "Supported EFI configuration table UUIDs:"
            for t in system_table.ConfigurationTable:
                print t.VendorGuid, known_uuids.get(t.VendorGuid.uuid, '')
            print
        except:
            print "Error printing EFI information:"
            import traceback
            traceback.print_exc()

# EFI_LOCATE_SEARCH_TYPE
AllHandles = 0
ByRegisterNotify = 1
ByProtocol = 2

def show_available_protocols():
    # Retrieve the list of all handles from the handle database
    handle_count = ctypes.c_ulong(0)
    handle_buffer = ctypes.c_void_p(0)
    status = call(system_table.BootServices.contents.LocateHandleBuffer, AllHandles, 0, 0, ctypes.addressof(handle_count), ctypes.addressof(handle_buffer))

    if status != EFI_SUCCESS:
        print "LocateHandleBuffer() failed, status = {:#x}".format(status)
        return

    try:
        protocols = set()

        handles = (ctypes.c_ulong * handle_count.value).from_address(handle_buffer.value)
        for handle in handles:
            # Retrieve the list of all the protocols on each handle
            guids_buffer = ctypes.c_void_p(0)
            protocol_count = ctypes.c_ulong(0)
            status = call(system_table.BootServices.contents.ProtocolsPerHandle, handle, ctypes.addressof(guids_buffer), ctypes.addressof(protocol_count))

            if status != EFI_SUCCESS:
                print "ProtocolsPerHandle() failed, status = {:#x}".format(status)
                continue

            try:
                protocol_guids = (ctypes.POINTER(GUID) * protocol_count.value).from_address(guids_buffer.value)
                for protocol_guid in protocol_guids:
                    protocols.add(protocol_guid.contents.uuid)
            finally:
                check_status(call(system_table.BootServices.contents.FreePool, guids_buffer.value))

        print 'EFI protocols in use (count={})'.format(len(protocols))
        for protocol in sorted(protocols):
            print protocol, known_uuids.get(protocol, '')
        print

    finally:
        check_status(call(system_table.BootServices.contents.FreePool, handle_buffer.value))

def save_tables():
    """Save all EFI tables to files.

    Warning: All files in the /efi_tables directory will be deleted!"""

    root = get_boot_fs()
    tables_dir = root.mkdir("efi_tables")
    # delete all files in \efi_tables directory
    for f in os.listdir("/efi_tables"):
        tables_dir.open(f, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE).delete()

    tables = dict(
        systemtable=system_table,
        configurationtable=system_table.ConfigurationTable,
        runtimeservices=system_table.RuntimeServices.contents,
        bootservices=system_table.BootServices.contents)
    for name, table in tables.iteritems():
        tables_dir.create("{}.bin".format(name)).write(to_bytes(table))
