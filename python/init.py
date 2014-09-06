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
#
# Portions based on site.py from Python 2.6, under the Python license.

"""Python initialization, to run at BITS startup."""

def early_init():
    # Set up redirection first, before importing anything else, so that any
    # errors in subsequent imports will get captured into the log.
    import redirect
    redirect.redirect()

    # Parse the ACPI SPCR and automatically set up the serial port if present
    serial_cmd = "false"
    try:
        import acpi
        spcr = acpi.parse_table("SPCR")
        if spcr is not None:
            addr = spcr.base_address
            speed = spcr.baud_rate_decode
            if addr.address_space_id == acpi.ASID_SYSTEM_IO and addr.register_bit_width == 8 and addr.address != 0 and speed is not None:
                port = addr.address
                serial_cmd = "serial --port={:#x} --speed={}".format(port, speed)
    except Exception as e:
        print "Error parsing Serial Port Console Redirect (SPCR) table:"
        print e

    import os
    os.environ["serial_cmd"] = serial_cmd

class _Helper(object):
    """Define the built-in 'help'."""

    def __repr__(self):
        return "Type help() for interactive help, " \
               "or help(object) for help about object."
    def __call__(self, *args, **kwds):
        import pydoc
        import redirect
        with redirect.nolog():
            return pydoc.help(*args, **kwds)

def init():
    import bitsconfig
    bitsconfig.init()

    import grubcmds
    grubcmds.register()

    import bits
    import os
    try:
        import acpi
        mcfg = acpi.parse_table("MCFG")
        if mcfg is None:
            print 'No ACPI MCFG Table found. This table is required for PCI Express.'
        else:
            for mcfg_resource in mcfg.resources:
                if mcfg_resource.segment == 0:
                    if mcfg_resource.address >= (1 << 32):
                        print "Error: PCI Express base above 32 bits is unsupported by BITS"
                        break
                    bits.pcie_set_base(mcfg_resource.address)
                    os.putenv('pciexbase', '{:#x}'.format(mcfg_resource.address))
                    os.putenv('pcie_startbus', '{:#x}'.format(mcfg_resource.start_bus))
                    os.putenv('pcie_endbus', '{:#x}'.format(mcfg_resource.end_bus))
                    break
            else:
                print "Error initializing PCI Express base from MCFG: no resource with segment 0"
    except Exception as e:
        print "Error occurred initializing PCI Express base from MCFG:"
        print e

    import readline
    readline.init()
    import rlcompleter_extra

    import testacpi
    testacpi.register_tests()
    try:
        import testefi
        testefi.register_tests()
    except ImportError as e:
        pass
    import testsmrr
    testsmrr.register_tests()
    import smilatency
    smilatency.register_tests()
    import mptable
    mptable.register_tests()

    from cpudetect import cpulib
    cpulib.register_tests()

    import testsuite
    testsuite.finalize_cfgs()

    import sysinfo
    sysinfo.log_sysinfo()
    import smbios
    smbios.log_smbios_info()
    try:
        import efi
        efi.log_efi_info()
    except ImportError as e:
        pass

    batch = bitsconfig.config.get("bits", "batch").strip()
    if batch:
        import redirect
        print "\nBatch mode enabled:", batch
        for batch_keyword in batch.split():
            print "\nRunning batch operation", batch_keyword
            try:
                if batch_keyword == "test":
                    testsuite.run_all_tests()
                with redirect.logonly():
                    if batch_keyword == "acpi":
                        import acpi
                        print acpi.dumptables()
                    if batch_keyword == "smbios":
                        import smbios
                        smbios.dump_raw()
            except:
                print "\nError in batch operation", batch_keyword
                import traceback
                traceback.print_exc()

        print "\nBatch mode complete\n"
        redirect.write_logfile("/boot/bits-log.txt")

    import __builtin__
    __builtin__.help = _Helper()

    import pydoc
    import ttypager
    pydoc.getpager = ttypager.getpager
