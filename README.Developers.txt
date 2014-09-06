This document, README.Developers.txt, provides documentation useful for the
prospective BITS developer.  See README.txt for user documentation, or
INSTALL.txt for instructions on creating a bootable USB disk.

Please send any bug reports, patches, or other mail about BITS to
Burt Triplett <burt@pbjtriplett.org>.

You can find the BITS homepage at http://biosbits.org/

BITS incorporates the following components:
- GRUB 2.00:
  - Two enhancements to the grub_printf function to support printf format
    string features needed by ACPICA
  - A bugfix for grub_printf to handle %% correctly.
  - Fix GRUB's handling of the pager environment variable, to always enable
    the pager when setting pager=1 and disable it otherwise, rather than
    incrementing and decrementing an internal variable.  The existing behavior
    required two "set pager=0" to turn off after two "set pager=1", and
    effectively enabled the pager when running an unmatched "set pager=0"
    (since -1 is non-zero) and disabling it with a subsequent "set pager=1".
  - Drop reference to gets from gnulib's stdio.h, to fix builds on systems that
    don't define gets at all.
  - Re-adding support to lnxboot to capture the boot device partition number,
    required when booting via syslinux.  Upstream GRUB dropped that code in bzr
    revno 3592.  Patch merged upstream in bzr revno 5185.
  - Fix the EFI memory allocator to actually round allocation sizes up
    to the next page as intended, not to the next 1k boundary.
  - Support allocating pages under 1M (GRUB_MMAP_MALLOC_LOW) in the EFI
    memory allocator (needed to allocate memory for an IPI handler).
  - Make the EFI firmware memory allocator translate requests for
    GRUB_MEMORY_AVAILABLE into EfiLoaderCode, not EfiConventionalMemory.
    The underlying EFI AllocatePages call will refuse attempts to
    allocate memory and leave it marked as EfiConventionMemory
    (free memory available for subsequent allocations).
  - Backport a subset of the changes from bzr revnos 4845, 4849, and
    4864, to make grub-mkrescue use newer xorriso to build a single .iso
    bootable as an EFI CD, EFI hard disk, BIOS CD, and BIOS hard disk.
  - On 64-bit EFI, align the stack to a 16-byte boundary as required by the
    x86-64 ABI before calling grub_main().  This eliminates crashes that would
    otherwise occur in various circumstances, such as when invoking SSE
    instructions.
- Python 2.7.5:
  - Build fixes to ctypes and libffi to build in the BITS environment.
- ACPICA version 20130517
- fdlibm 5.3


BITS scripting
==============

BITS includes the Python interpreter, and BITS tests and other functionality
use Python whenever possible.  In addition to a subset of the Python standard
library, BITS provides additional Python modules supporting access to platform
functionality such as CPUID, MSRs, PCI, and ACPI.

You can run arbitrary Python from the GRUB command line using the 'py' command;
you'll need to quote its argument so GRUB passes it as a single uninterpreted
string.  For example:

grub> py 'print "Hello world!"'

BITS loads Python modules from /boot/python, and you can add your own modules
there as well.

The standard Python library lives in /boot/python/lib; to include more modules
from the standard library, edit the build script.  Remember to include any
other modules imported by the one you want, recursively.

Low-level Python functions implemented in C live in the _bits module, defined
in rc/python/bitsmodule.c.  Define new C functionality there, and re-export or
wrap it in the bits module.

BITS automatically generates the test menu from all the available tests for the
current system.  To add new tests to the test menu, call testsuite.add_test on
them from the register_tests() function of an appropriate test* module (for
non-CPU-specific tests) or of the cpu_* module for a particular target CPU (for
CPU-specific tests).  To add a new test module, call its register_tests()
function from init.init().

Note that if you edit scripts directly on your USB disk, and then rebuild your
USB disk by running ./mkdisk, your scripts will get overwritten.  Edit them in
the BITS source tree instead, or save a separate copy of them before running
./mkdisk.  Even better, they might prove more generally useful, so send them
along to get incorporated into BITS.

Also note that BITS pre-compiles all of its Python code to bytecode files
(.pyc); the version of GRUB2 used by BITS does not support file modification
times, so Python's usual check for whether the source code matches the .pyc
file will not work.  If you edit a .py file directly on your USB disk, you'll
need to remove the corresponding .pyc file manually, or your changes will not
take effect.  However, see above about making changes in the BITS source tree
instead.


Building BITS from source
=========================

BITS builds as a set of additional modules and commands for GNU GRUB2.
Accompanying the BITS source distribution, you will find an archive of the
version of GNU GRUB2 used to build BITS.  You will need to unpack both that
source code and BITS in separate directories.  Note that BITS requires a
specific snapshot of GRUB2 (see README.txt for the bzr revision number), and
will not necessarily build with the latest version of GRUB2.

GRUB2's build system for external modules requires BITS to know the path to
GRUB2's source directory and the BITS source directory.  The BITS build script
assumes you unpacked the source code for GRUB2 at ../grub relative to the BITS
source directory.  If you unpack the GRUB2 source code to some other path,
export grub_src=/path/to/grub before running ./build.

BITS also depends on ACPICA and Python, and the BITS distribution includes an
unmodified copy of the versions of ACPICA and Python that BITS needs.  You will
need to unpack these source archives to the directories ../acpica-unix2 and
../Python relative to the BITS source directory.  BITS does not currently
support putting those source directories anywhere else.

To support floating-point functions required by Python, BITS requires fdlibm,
the "Freely Distributable libm".  BITS uses a slightly modified copy of fdlibm,
to fix some known issues which trigger compiler warnings.  The BITS
distribution includes an archive of the necessary fdlibm source code; you will
need to unpack that source archive to ../fdlibm relative to the BITS source
directory.

GRUB2 itself has a few build dependencies; review the file "INSTALL" in the
GRUB2 source code for a full list.  Note that because BITS provides additional
GRUB modules and thus extends the GRUB build system, you will need the
additional tools described as required for development snapshots or hacking on
GRUB.

BITS requires GNU binutils 2.20 or newer, due to a bug in the GNU assembler in
older versions which causes it to incorrectly assemble parts of BITS.

The BITS build procedure requires the following additional build dependencies:
- tofrodos, for the "todos" command.
- xorriso 1.3.0 or newer, to construct an .iso image using
  grub-mkrescue.  Older versions will fail to recognize the options
  needed to build a single .iso bootable as an EFI CD, EFI hard disk,
  BIOS CD, and BIOS hard disk.
- mtools, to construct the EFI image embedded in an EFI-bootable .iso

If you build BITS repeatedly, you'll want to install and configure ccache to
speed up these repeated builds.

Once you have the source code unpacked and the build dependencies installed,
you can build BITS by running ./build in the top of the BITS source tree.  This
will produce a binary distribution of BITS as a zip file, which includes
installation instructions (INSTALL.txt) and full corresponding source code.
Read INSTALL.txt for more information on building a bootable USB disk,
including the additional software you will need to do so.

Once you have a bootable USB disk, you can quickly update that disk to include
a new version of BITS by running ./mkdisk after building.  NOTE: ./mkdisk
assumes you have a USB disk /dev/sdb with a partition /dev/sdb1 that you want
to use for BITS.  If you want to use some device other than /dev/sdb, EDIT
MKDISK FIRST!  mkdisk will refuse to write to a non-removable disk as a safety
check; if for some reason you want to write to a non-removable disk, you'll
have to comment out that check as well.  Sorry for making it inconvenient to
overwrite your hard disk.


Coding style
============

C code in BITS follows the K&R coding style, with four space indents, and no
tabs.

Python code in BITS should follow PEP 8
(http://www.python.org/dev/peps/pep-0008/), the Style Guide for Python Code,
which also specifies four-space indents.

Don't try to wrap lines to fit an arbitrary line width, but do wrap lines when
it improves readability by lining up similar code.

The script "bits-indent" roughly approximates the right style for C code,
except that it will un-wrap all lines, even those which look better wrapped.
Use good taste.
