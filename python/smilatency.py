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

"""SMI latency test."""

import bits
from collections import namedtuple
import testsuite
import time
import usb

def register_tests():
    testsuite.add_test("SMI latency test", smi_latency);
    testsuite.add_test("SMI latency test with USB disabled via BIOS handoff", test_with_usb_disabled, runall=False);

def smi_latency():
    IA32_TSC_MSR = 0x10
    MSR_SMI_COUNT = 0x34
    bsp_apicid = bits.bsp_apicid()

    if bits.rdmsr(bsp_apicid, IA32_TSC_MSR) is None:
        raise RuntimeError("Reading of IA32_TSC MSR caused a GPF")

    print "Warning: touching the keyboard can affect the results of this test."
    print "Starting pass #1.  Calibrating the TSC."

    start = time.time()
    tsc1 = bits.rdmsr(bsp_apicid, IA32_TSC_MSR)

    while time.time() - start < 1:
        pass

    stop = time.time()
    tsc2 = bits.rdmsr(bsp_apicid, IA32_TSC_MSR)

    tsc_per_sec = (tsc2 - tsc1) / (stop - start)
    tsc_per_usec = tsc_per_sec / (1000*1000)

    def show_time(tscs):
        units = [(1000*1000*1000, "ns"), (1000*1000, "us"), (1000, "ms")]
        for divisor, unit in units:
            temp = tscs / (tsc_per_sec / divisor)
            if temp < 10000:
                return "{}{}".format(int(temp), unit)
        return "{}s".format(int(tscs / tsc_per_sec))

    bins = [long(tsc_per_usec * 10**i) for i in range(9)]
    bin_descs = [
        "0     < t <=   1us",
        "1us   < t <=  10us",
        "10us  < t <= 100us",
        "100us < t <=   1ms",
        "1ms   < t <=  10ms",
        "10ms  < t <= 100ms",
        "100ms < t <=   1s ",
        "1s    < t <=  10s ",
        "10s   < t <= 100s ",
        "100s  < t         ",
    ]

    print "Starting pass #2.  Wait here, I will be back in 15 seconds."
    (max_latency, smi_count_delta, bins) = bits.smi_latency(long(15 * tsc_per_sec), bins)
    BinType = namedtuple('BinType', ("max", "total", "count", "times"))
    bins = [BinType(*b) for b in bins]

    testsuite.test("SMI latency < 150us to minimize risk of OS timeouts", max_latency / tsc_per_usec <= 150)
    if not testsuite.show_detail():
        return

    for bin, desc in zip(bins, bin_descs):
        if bin.count == 0:
            continue
        testsuite.print_detail("{}; average = {}; count = {}".format(desc, show_time(bin.total/bin.count), bin.count))
        deltas = (show_time(t2 - t1) for t1,t2 in zip(bin.times, bin.times[1:]))
        testsuite.print_detail(" Times between first few observations: {}".format(" ".join("{:>6}".format(delta) for delta in deltas)))

    if smi_count_delta is not None:
        testsuite.print_detail("{} SMI detected using MSR_SMI_COUNT (MSR {:#x})".format(smi_count_delta, MSR_SMI_COUNT))

    testsuite.print_detail("Summary of impact: observed maximum latency = {}".format(show_time(max_latency)))

def test_with_usb_disabled():
    if usb.handoff_to_os():
        smi_latency()
