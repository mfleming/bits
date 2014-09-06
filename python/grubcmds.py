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

"""GRUB commands implemented in Python."""

# All of the commands and other functions in this module have no value from
# Python, only from GRUB; only .register() needs to get called, at
# initialization time.
__all__ = ["register"]

import bits, sys, os, argparse, struct, testcpuid, testmsr, testpci, testsuite, testutil, time

def cmd_pydoc(args):
    import pydoc, os
    try:
        oldargv = sys.argv
    except AttributeError:
        oldargv = None
    oldpath = sys.path
    oldterm = os.getenv("TERM")
    try:
        sys.argv = args
        sys.path = [''] + oldpath
        os.putenv("TERM", "dumb")
        pydoc.cli()
    finally:
        if oldargv is None:
            del sys.argv
        else:
            sys.argv = oldargv
        sys.path = oldpath
        if oldterm is None:
            os.unsetenv("TERM")
        else:
            os.putenv("TERM", oldterm)

def parse_int(s, name, max_bound=None, min_bound=None):
    modified = s
    if s.endswith('h') or s.endswith('H'):
        modified = '0x' + s.rstrip('hH')
    try:
        value = int(modified, 0)
    except ValueError:
        raise argparse.ArgumentTypeError("invalid {0} value: {1!r}".format(name, s))
    if value < 0:
        raise argparse.ArgumentTypeError("{0} value must not be negative: {1!r}".format(name, s))
    if max_bound is not None and value > max_bound:
        raise argparse.ArgumentTypeError("{0} value too large: {1!r}".format(name, s))
    if min_bound is not None and value < min_bound:
        raise argparse.ArgumentTypeError("{0} value too small: {1!r}".format(name, s))
    return value

def parse_shift(s):
    return parse_int(s, "shift", 63)

def parse_mask(s):
    return parse_int(s, "mask", 2**64 - 1)

def parse_msr(s):
    return parse_int(s, "MSR", 2**32 - 1)

def parse_msr_value(s):
    return parse_int(s, "VALUE", 2**64 - 1)

def parse_function(s):
    return parse_int(s, "FUNCTION", 2**32 - 1)

def parse_index(s):
    return parse_int(s, "INDEX", 2**32 - 1)

def parse_iterations(s):
    return parse_int(s, "ITERATIONS")

def parse_pciexbase(s):
    return parse_int(s, "pciexbase", 2**32 - 1, 1)

def parse_pci_bus(s):
    return parse_int(s, "BUS", 255)

def parse_pci_dev(s):
    return parse_int(s, "DEVICE", 2**5 - 1)

def parse_pci_fun(s):
    return parse_int(s, "FUNCTION", 2**3 - 1)

def parse_pci_reg(s):
    return parse_int(s, "REGISTER", 2**8 - 1)

def parse_pcie_reg(s):
    return parse_int(s, "REGISTER", 2**12 - 1)

def parse_pci_value(s):
    return parse_int(s, "VALUE", 2**64 - 1)

def parse_verbose(s):
    return parse_int(s, "verbose", 3)

ALL_CPUS = ~0

def parse_cpu(s):
    if s.lower() == 'all':
        return ALL_CPUS
    return parse_int(s, "CPU", max_bound = len(bits.cpus())-1)

def each_apicid(cpu):
    if cpu is None:
        cpu = os.environ.get("viewpoint", ALL_CPUS)
        if cpu != ALL_CPUS:
            cpu = int(cpu, 0)
    cpus = bits.cpus()
    if cpu == ALL_CPUS:
        for apicid in cpus:
            yield apicid
    else:
        yield cpus[cpu]

def parse_usec(s):
    return parse_int(s, "VALUE", 2**32 - 1, 1)

brandstring_argparser = argparse.ArgumentParser(prog='brandstring', description='Display brand string obtained via CPUID instruction')
brandstring_argparser.add_argument('-c', '--cpu', type=parse_cpu, help='CPU number')

def cmd_brandstring(args):
    uniques = {}
    for cpu in each_apicid(args.cpu):
        brand = ""
        if bits.cpuid(cpu, 0x80000000).eax >= 0x80000004:
            brand = "".join(struct.pack("<LLLL", *bits.cpuid(cpu, func_num)) for func_num in range(0x80000002, 0x80000005))
            brand = brand.rstrip('\0')
        uniques.setdefault(brand, []).append(cpu)
    for value in sorted(uniques.iterkeys()):
        cpus = uniques[value]
        print 'Brandstring: "{0}"'.format(value)
        print "On {0} CPUs: {1}".format(len(cpus), testutil.apicid_list(cpus))

cpu_sleep_argparser = argparse.ArgumentParser(prog='cpu_sleep', description='Sleep using mwait')
cpu_sleep_argparser.add_argument('usec', type=parse_usec, help='Time to sleep in microseconds')

def cmd_cpu_sleep(args):
    bits.blocking_sleep(args.usec)

cpu_argparser = argparse.ArgumentParser(prog='cpu', description='Set CPU number or optionally list CPU numbers')
cpu_argparser.add_argument('-e', '--env', action='store_true', help='Set $apicid_list to list of APIC IDs (default=disabled)')
cpu_argparser.add_argument('-l', '--list', action='store_true', help='List CPU numbers')
cpu_argparser.add_argument('-q', '--quiet', action='store_true', help='No display (default=disabled)')
cpu_argparser.add_argument('num', type=parse_cpu, help='Assigned CPU number (default=all)', nargs='?')

def cmd_cpu(args):
    cpus = bits.cpus()
    ncpus = len(cpus)
    if args.num is not None:
        if args.num == ALL_CPUS:
            os.unsetenv("viewpoint")
        else:
            os.putenv("viewpoint", str(args.num))
    if args.list:
        print "{} CPUs: 0-{}".format(ncpus, ncpus-1)
        for i in range(ncpus):
            # print 4 per line
            print 'CPU{: <3d} = {: <#10x}'.format(i, cpus[i]),
            if i == (ncpus - 1) or (i % 4) == 3:
                print
    if not args.quiet:
        print "Assigned CPU number = {}".format(os.environ.get("viewpoint", 'all'))
    if args.env:
        os.putenv("apicid_list", " ".join("{:#x}".format(cpu) for cpu in sorted(cpus)))

cpuid32_argparser = argparse.ArgumentParser(prog='cpuid32', description='Display registers returned by CPUID instruction')
cpuid32_argparser.add_argument('-c', '--cpu', type=parse_cpu, help='CPU number')
cpuid32_argparser.add_argument('-e', '--env', action='store_true', help='Set environment vars eax,ebx,ecx,edx (default=disabled)')
cpuid32_argparser.add_argument('-q', '--quiet', action='store_true', help='No display of register values (default=disabled)')
cpuid32_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to values read (default=~0)')
cpuid32_argparser.add_argument('-A', '--eax-mask', default=~0, type=parse_mask, help='Mask to apply to EAX; overrides --mask (default=~0)', metavar='MASK')
cpuid32_argparser.add_argument('-B', '--ebx-mask', default=~0, type=parse_mask, help='Mask to apply to EBX; overrides --mask (default=~0)', metavar='MASK')
cpuid32_argparser.add_argument('-C', '--ecx-mask', default=~0, type=parse_mask, help='Mask to apply to ECX; overrides --mask (default=~0)', metavar='MASK')
cpuid32_argparser.add_argument('-D', '--edx-mask', default=~0, type=parse_mask, help='Mask to apply to EDX; overrides --mask (default=~0)', metavar='MASK')
cpuid32_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
cpuid32_argparser.add_argument('function', type=parse_function, help='Function number used in EAX')
cpuid32_argparser.add_argument('index', type=parse_index, help='Index number used in ECX', nargs='?')

def cmd_cpuid32(args):
    if args.cpu is not None:
        args.cpu = bits.cpus()[args.cpu]
    uniques, desc = testcpuid.cpuid_helper(args.function, args.index, args.cpu, args.shift, args.mask, args.eax_mask, args.ebx_mask, args.ecx_mask, args.edx_mask)
    if args.env:
        if len(uniques) > 1:
            print "Setting environment vars eax,ebx,ecx,edx requested; but register values are not unique"
            return False
        for regname, regvalue in uniques.keys()[0]._asdict().iteritems():
            os.putenv(regname, "{:#x}".format(regvalue))
    if not args.quiet:
        print "\n".join(desc)
    return True

format_argparser = argparse.ArgumentParser(prog='format', description='Format numeric environment variable')
format_group = format_argparser.add_mutually_exclusive_group(required=True)
format_group.add_argument('-d', '--dec', action='store_true', dest='decimal', help='Format as decimal')
format_group.add_argument('-x', '--hex', action='store_false', dest='decimal', help='Format as hexadecimal')
format_argparser.add_argument('variable', nargs='+', help='Name of an environment variable to format')

def cmd_format(args):
    for variable in args.variable:
        os.environ[variable] = ["{:#x}", "{:d}"][args.decimal].format(int(os.environ[variable], 0))

msr_available_argparser = argparse.ArgumentParser(prog='msr_available', description='MSR available (does not GPF on read)')
msr_available_argparser.add_argument('msr', type=parse_msr, help='MSR number')

def cmd_msr_available(args):
    return testmsr.msr_available(args.msr)

def do_pci_write(args, pci_read_func, pci_write_func, **extra_args):
    size = bits.addr_alignment(args.reg)
    if args.bytes is not None:
        size = args.bytes

    args.adj_value = value = (args.value & args.mask) << args.shift
    if args.rmw:
        value = value | (pci_read_func(args.bus, args.dev, args.fn, args.reg, bytes=size, **extra_args) & ~(args.mask << args.shift))
    pci_write_func(args.bus, args.dev, args.fn, args.reg, value, bytes=size, **extra_args)

    if not args.quiet:
        args.op = '='
        if args.rmw:
            args.op = '|='
        prefix = "PCI {bus:#04x}:{dev:#04x}.{fn:#03x} reg {reg:#04x} {op}".format(**vars(args))
        if args.mask == ~0:
            if args.shift == 0:
                print prefix, "{value:#x}".format(**vars(args))
            else:
                print prefix, "{value:#x} << {shift} ({adj_value:#x})".format(**vars(args))
        else:
            print prefix, "({value:#x} & {mask}) << {shift} ({adj_value:#x})".format(**vars(args))

    return True

pci_read_argparser = argparse.ArgumentParser(prog='pci_read', description='Read PCI register')
pci_read_argparser.add_argument('-b', '--bytes', type=int, choices=[1,2,4], help='Bytes to read for PCI value')
pci_read_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
pci_read_argparser.add_argument('-q', '--quiet', action='store_true', help='No display unless failures occur (default=disabled)')
pci_read_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
pci_read_argparser.add_argument('-v', '--varname', help='Save read value into variable VARNAME')
pci_read_argparser.add_argument('bus', type=parse_pci_bus, help='Bus number')
pci_read_argparser.add_argument('dev', type=parse_pci_dev, help='Device number')
pci_read_argparser.add_argument('fn', type=parse_pci_fun, help='Function number')
pci_read_argparser.add_argument('reg', type=parse_pci_reg, help='Register number')

def cmd_pci_read(args):
    value, desc = testpci.pci_read_helper(args.bus, args.dev, args.fn, args.reg, pci_read_func=bits.pci_read, bytes=args.bytes, mask=args.mask, shift=args.shift)
    if not args.quiet:
        print desc
    if args.varname is not None:
        os.putenv(args.varname, "{:#x}".format(value))
    return True

pci_write_argparser = argparse.ArgumentParser(prog='pci_write', description='Write PCI register')
pci_write_argparser.add_argument('-b', '--bytes', type=int, choices=[1,2,4], help='Bytes to write for PCI value')
pci_write_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
pci_write_argparser.add_argument('-q', '--quiet', action='store_true', help='No display unless failures occur (default=disabled)')
pci_write_argparser.add_argument('-r', '--rmw', action='store_true', help='Read-modify-write operation (default=disabled)')
pci_write_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
pci_write_argparser.add_argument('bus', type=parse_pci_bus, help='Bus number')
pci_write_argparser.add_argument('dev', type=parse_pci_dev, help='Device number')
pci_write_argparser.add_argument('fn', type=parse_pci_fun, help='Function number')
pci_write_argparser.add_argument('reg', type=parse_pci_reg, help='Register number')
pci_write_argparser.add_argument('value', type=parse_pci_value, help='Value to write')

def cmd_pci_write(args):
    return do_pci_write(args, bits.pci_read, bits.pci_write)

def get_pciexbase(memaddr_arg):
    """Get the pciexbase from the environment or the specified argument.

    The argument takes precedence if not None."""
    if memaddr_arg is not None:
        return memaddr_arg
    baseaddr = os.getenv("pciexbase")
    if baseaddr is None:
        print "No PCIE memory base address specified."
        return None
    try:
        baseaddr = parse_int(baseaddr, "pciexbase environment variable", 2**32 - 1)
    except argparse.ArgumentTypeError:
        print sys.exc_info()[1]
        return None
    return baseaddr

pcie_read_argparser = argparse.ArgumentParser(prog='pcie_read', description='Read PCIE register')
pcie_read_argparser.add_argument('-b', '--bytes', type=int, choices=[1,2,4,8], help='Bytes to read for PCIe value')
pcie_read_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
pcie_read_argparser.add_argument('-p', '--memaddr', type=parse_pciexbase, help='PCIE memory base address (default=$pciexbase)')
pcie_read_argparser.add_argument('-q', '--quiet', action='store_true', help='No display unless failures occur (default=disabled)')
pcie_read_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
pcie_read_argparser.add_argument('-v', '--varname', help='Save read value into variable VARNAME')
pcie_read_argparser.add_argument('bus', type=parse_pci_bus, help='Bus number')
pcie_read_argparser.add_argument('dev', type=parse_pci_dev, help='Device number')
pcie_read_argparser.add_argument('fn', type=parse_pci_fun, help='Function number')
pcie_read_argparser.add_argument('reg', type=parse_pcie_reg, help='Register number')

def cmd_pcie_read(args):
    baseaddr = get_pciexbase(args.memaddr)
    if baseaddr is None:
        return False
    value, desc = testpci.pci_read_helper(args.bus, args.dev, args.fn, args.reg, pci_read_func=bits.pcie_read, bytes=args.bytes, mask=args.mask, shift=args.shift, memaddr=baseaddr)
    if not args.quiet:
        print desc
    if args.varname is not None:
        os.putenv(args.varname, "{:#x}".format(value))
    return True

pcie_write_argparser = argparse.ArgumentParser(prog='pcie_write', description='Write PCIE register')
pcie_write_argparser.add_argument('-b', '--bytes', type=int, choices=[1,2,4,8], help='Bytes to write for PCIE value')
pcie_write_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
pcie_write_argparser.add_argument('-p', '--memaddr', type=parse_pciexbase, help='PCIE memory base address (default=$pciexbase)')
pcie_write_argparser.add_argument('-q', '--quiet', action='store_true', help='No display unless failures occur (default=disabled)')
pcie_write_argparser.add_argument('-r', '--rmw', action='store_true', help='Read-modify-write operation (default=disabled)')
pcie_write_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
pcie_write_argparser.add_argument('bus', type=parse_pci_bus, help='Bus number')
pcie_write_argparser.add_argument('dev', type=parse_pci_dev, help='Device number')
pcie_write_argparser.add_argument('fn', type=parse_pci_fun, help='Function number')
pcie_write_argparser.add_argument('reg', type=parse_pcie_reg, help='Register number')
pcie_write_argparser.add_argument('value', type=parse_pci_value, help='Value to write')

def cmd_pcie_write(args):
    baseaddr = get_pciexbase(args.memaddr)
    if baseaddr is None:
        return False
    return do_pci_write(args, bits.pcie_read, bits.pcie_write, memaddr=baseaddr)

rdmsr_argparser = argparse.ArgumentParser(prog='rdmsr', description='Read MSR')
rdmsr_argparser.add_argument('-q', '--quiet', action='store_true', help='No display unless failures occur (default=disabled)')
rdmsr_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
rdmsr_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
rdmsr_argparser.add_argument('-v', '--varname', help='Save read value into variable VARNAME')
rdmsr_argparser.add_argument('-c', '--cpu', type=parse_cpu, help='CPU number')
rdmsr_argparser.add_argument('msr', type=parse_msr, help='MSR number')

def cmd_rdmsr(args):
    if args.cpu is not None:
        args.cpu = bits.cpus()[args.cpu]
    uniques, desc = testmsr.rdmsr_helper(msr=args.msr, cpu=args.cpu, shift=args.shift, mask=args.mask)
    if not args.quiet:
        print "\n".join(desc)
    value = uniques.keys()[0]
    if args.varname is not None:
        if len(uniques) > 1:
            print "Variable setting requested but MSR value not unique"
            return False
        if value is None:
            print "Variable setting requested but MSR read caused GPF"
            return False
        os.putenv(args.varname, "{0:#x}".format(value))
    return len(uniques) == 1 and value is not None

def parse_hint(s):
    return parse_int(s, "HINT")

set_mwait_argparser = argparse.ArgumentParser(prog='set_mwait', usage='%(prog)s [-c cpu_num] [disable | [-i] enable hint]',
        description='Set MWAIT disable/enable, hint, and interrupt break event')
set_mwait_argparser.add_argument('-c', '--cpu', type=parse_cpu, help='CPU number')
set_mwait_argparser.add_argument('-i', '--no-int-break-event', action='store_true', help='Interrupt Break Event Disable (default=enabled)')
set_mwait_argparser.add_argument('mwait', type=str, choices=['enable', 'disable'], help='Enable or disable MWAIT')
set_mwait_argparser.add_argument('hint', nargs='?', type=parse_hint, help='Hint value for MWAIT')

def cmd_set_mwait(args):
    if args.mwait == 'enable':
        if args.hint is None:
            set_mwait_argparser.print_usage()
            print '"enable" requires an MWAIT hint value'
            return
        use_mwait = True

    if args.mwait == 'disable':
        if args.hint is not None or args.no_int_break_event:
            set_mwait_argparser.print_usage()
            print '"disable" takes no arguments'
            return
        use_mwait = False
        args.hint = 0

    for apicid in each_apicid(args.cpu):
        bits.set_mwait(apicid, use_mwait, args.hint, not args.no_int_break_event)

test_cpuid_consistent_argparser = argparse.ArgumentParser(prog='test_cpuid_consistent', description='Test for consistent registers returned by CPUID instructions')
test_cpuid_consistent_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to values read (default=~0)')
test_cpuid_consistent_argparser.add_argument('-A', '--eax-mask', default=~0, type=parse_mask, help='Mask to apply to EAX; overrides --mask (default=~0)', metavar='MASK')
test_cpuid_consistent_argparser.add_argument('-B', '--ebx-mask', default=~0, type=parse_mask, help='Mask to apply to EBX; overrides --mask (default=~0)', metavar='MASK')
test_cpuid_consistent_argparser.add_argument('-C', '--ecx-mask', default=~0, type=parse_mask, help='Mask to apply to ECX; overrides --mask (default=~0)', metavar='MASK')
test_cpuid_consistent_argparser.add_argument('-D', '--edx-mask', default=~0, type=parse_mask, help='Mask to apply to EDX; overrides --mask (default=~0)', metavar='MASK')
test_cpuid_consistent_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
test_cpuid_consistent_argparser.add_argument('text', type=str, help='Test description text')
test_cpuid_consistent_argparser.add_argument('function', type=parse_function, help='Function number used in EAX')
test_cpuid_consistent_argparser.add_argument('index', type=parse_index, help='Index number used in ECX', nargs='?')

def cmd_test_cpuid_consistent(args):
    return testcpuid.test_cpuid_consistency(args.text, args.function, args.index, args.shift, args.mask, args.eax_mask, args.ebx_mask, args.ecx_mask, args.edx_mask)

test_pci_argparser = argparse.ArgumentParser(prog='test_pci', description='Test PCI config space values')
test_pci_argparser.add_argument('-b', '--bytes', type=int, choices=[1,2,4], help='Bytes to read for PCI value')
test_pci_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
test_pci_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
test_pci_argparser.add_argument('text', type=str, help='Test description text')
test_pci_argparser.add_argument('bus', type=parse_pci_bus, help='Bus number')
test_pci_argparser.add_argument('dev', type=parse_pci_dev, help='Device number')
test_pci_argparser.add_argument('fn', type=parse_pci_fun, help='Function number')
test_pci_argparser.add_argument('reg', type=parse_pci_reg, help='Register number')
test_pci_argparser.add_argument('expected_value', type=parse_pci_value, help='Expected value')

def cmd_test_pci(args):
    return testpci.test_pci(args.text, args.bus, args.dev, args.fn, args.reg, args.expected_value, bytes=args.bytes, mask=args.mask, shift=args.shift)

test_msr_argparser = argparse.ArgumentParser(prog='test_msr', description='Test MSR values')
test_msr_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
test_msr_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
test_msr_argparser.add_argument('-c', '--cpu', type=parse_cpu, help='CPU number')
test_msr_argparser.add_argument('text', type=str, help='Test description text')
test_msr_argparser.add_argument('msr', type=parse_msr, help='MSR number')
test_msr_argparser.add_argument('expected_value', type=parse_msr_value, help='Expected value')

def cmd_test_msr(args):
    if args.cpu is not None:
        args.cpu = bits.cpus()[args.cpu]
    return testmsr.test_msr(**vars(args))

test_msr_consistency_argparser = argparse.ArgumentParser(prog='test_msr_consistency', description='Test MSRs for consistency across CPUs')
test_msr_consistency_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
test_msr_consistency_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
test_msr_consistency_argparser.add_argument('text', type=str, help='Test description text')
test_msr_consistency_argparser.add_argument('first_msr', type=parse_msr, help='First MSR number')
test_msr_consistency_argparser.add_argument('last_msr', type=parse_msr, help='Last MSR number (default=First MSR number)', nargs='?')

def cmd_test_msr_consistency(args):
    return testmsr.test_msr_consistency(**vars(args))

test_summary_argparser = argparse.ArgumentParser(prog='test_summary', description='Summarize and reset test suite results')

def cmd_test_summary(args):
    testsuite.summary()

timer_argparser = argparse.ArgumentParser(prog='timer', usage='%(prog)s start | stop [-q] [-v VARNAME] [iterations]',
        description='Start or stop a timer.  Use "timer start" to start the timer, recording the current time.  Use "timer stop" to stop the timer, computing the elapsed time in milliseconds.  Specifying iterations causes "timer stop" to compute iterations per millisecond.')
timer_argparser.add_argument('-q', '--quiet', action='store_true', help='No display of elapsed time (default=disabled)')
timer_argparser.add_argument('-v', '--varname', help='Save elapsed time in milliseconds into $VARNAME')
timer_argparser.add_argument('operation', type=str, choices=['start', 'stop'], help='Start or stop timer')
timer_argparser.add_argument('iterations', nargs='?', type=parse_iterations, help='Number of iterations')

timer_start = None

def cmd_timer(args):
    global timer_start

    if args.operation == 'start':
        if args.iterations is not None:
            timer_argparser.print_usage()
            print '"start" takes no arguments'
            return
        timer_start = time.time()

    if args.operation == 'stop':
        if timer_start is None:
            print '"stop" requires "start" on a prior call to start the timer'
            return
        stop = time.time()
        elapsed_ms = int((stop - timer_start) * 1000)
        timer_start = None
        if not args.quiet:
            itermsg = ""
            if args.iterations is not None:
                try:
                    itermsg = "; with {0} iteration/ms".format(int(round(args.iterations / elapsed_ms)))
                except ZeroDivisionError:
                    itermsg = "; cannot compute iterations/ms"
            print "elapsed time = {0} ms{1}".format(elapsed_ms, itermsg)
        if args.varname is not None:
            os.putenv(args.varname, "{0}".format(elapsed_ms))

wrmsr_argparser = argparse.ArgumentParser(prog='wrmsr', description='Write MSR')
wrmsr_argparser.add_argument('-q', '--quiet', action='store_true', help='No display unless failures occur (default=disabled)')
wrmsr_argparser.add_argument('-s', '--shift', default=0, type=parse_shift, help='Shift count for mask and value (default=0)')
wrmsr_argparser.add_argument('-m', '--mask', default=~0, type=parse_mask, help='Mask to apply to value (default=~0)')
wrmsr_argparser.add_argument('-r', '--rmw', action='store_true', help='Read-modify-write operation (default=disabled)')
wrmsr_argparser.add_argument('-c', '--cpu', type=parse_cpu, help='CPU number')
wrmsr_argparser.add_argument('msr', type=parse_msr, help='MSR number')
wrmsr_argparser.add_argument('value', type=parse_msr_value, help='MSR value')

def cmd_wrmsr(args):
    rd_fail = []
    wr_fail = []
    success = []

    def process_wrmsr(apicid):
        wr_value = 0
        if args.rmw:
            rd_value = bits.rdmsr(apicid, args.msr)
            if rd_value is None:
                rd_fail.append(apicid)
                return
            wr_value = rd_value & ~(args.mask << args.shift)
        wr_value |= (args.value & args.mask) << args.shift
        if bits.wrmsr(apicid, args.msr, wr_value):
            success.append(apicid)
        else:
            wr_fail.append(apicid)

    for apicid in each_apicid(args.cpu):
        process_wrmsr(apicid)

    if rd_fail or wr_fail or not args.quiet:
        if args.rmw:
            op = "|="
        else:
            op = "="
        print "MSR {0:#x} {1} ({2:#x} & {3:#x}) << {4} ({5:#x})".format(args.msr, op, args.value,
                                                                        args.mask, args.shift,
                                                                        (args.value & args.mask) << args.shift)
        if rd_fail:
            print "Read MSR fail (GPF) on {} CPUs: {}".format(len(rd_fail), testutil.apicid_list(rd_fail))
        if wr_fail:
            print "Write MSR fail (GPF) on {} CPUs: {}".format(len(wr_fail), testutil.apicid_list(wr_fail))
        if success:
            print "Write MSR pass on {} CPUs: {}".format(len(success), testutil.apicid_list(success))

    return not rd_fail and not wr_fail

def register_argparsed_command(func, argparser):
    usage = argparser.format_usage().split(' ', 2)[2].rstrip()
    def do_cmd(args):
        try:
            parsed_args = argparser.parse_args(args[1:])
        except SystemExit:
            return False
        return func(parsed_args)
    bits.register_grub_command(argparser.prog, do_cmd, usage, argparser.description)

def register():
    bits.register_grub_command("pydoc", cmd_pydoc, "NAME ... | -k KEYWORD", "Show Python documentation on a NAME or KEYWORD")
    register_argparsed_command(cmd_brandstring, brandstring_argparser)
    register_argparsed_command(cmd_cpu_sleep, cpu_sleep_argparser)
    register_argparsed_command(cmd_cpu, cpu_argparser)
    register_argparsed_command(cmd_cpuid32, cpuid32_argparser)
    register_argparsed_command(cmd_format, format_argparser)
    register_argparsed_command(cmd_msr_available, msr_available_argparser)
    register_argparsed_command(cmd_pci_read, pci_read_argparser)
    register_argparsed_command(cmd_pci_write, pci_write_argparser)
    register_argparsed_command(cmd_pcie_read, pcie_read_argparser)
    register_argparsed_command(cmd_pcie_write, pcie_write_argparser)
    register_argparsed_command(cmd_rdmsr, rdmsr_argparser)
    register_argparsed_command(cmd_set_mwait, set_mwait_argparser)
    register_argparsed_command(cmd_test_cpuid_consistent, test_cpuid_consistent_argparser)
    register_argparsed_command(cmd_test_pci, test_pci_argparser)
    register_argparsed_command(cmd_test_msr, test_msr_argparser)
    register_argparsed_command(cmd_test_msr_consistency, test_msr_consistency_argparser)
    register_argparsed_command(cmd_test_summary, test_summary_argparser)
    register_argparsed_command(cmd_timer, timer_argparser)
    register_argparsed_command(cmd_wrmsr, wrmsr_argparser)
