# Copyright (c) 2006-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Kevin Lim

from m5.objects import *
from Benchmarks import *

class CowIdeDisk(IdeDisk):
    image = CowDiskImage(child=RawDiskImage(read_only=True),
                         read_only=False)

    def childImage(self, ci):
        self.image.child.image_file = ci

class MemBus(Bus):
    badaddr_responder = BadAddr()
    default = Self.badaddr_responder.pio


def makeLinuxAlphaSystem(mem_mode, mdesc = None):
    class BaseTsunami(Tsunami):
        ethernet = NSGigE(pci_bus=0, pci_dev=1, pci_func=0)
        ide = IdeController(disks=[Parent.disk0, Parent.disk2],
                            pci_func=0, pci_dev=0, pci_bus=0)

    self = LinuxAlphaSystem()
    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.readfile = mdesc.script()
    self.iobus = Bus(bus_id=0)
    self.membus = MemBus(bus_id=1)
    self.bridge = Bridge(delay='50ns', nack_delay='4ns')
    self.physmem = PhysicalMemory(range = AddrRange(mdesc.mem()))
    self.bridge.side_a = self.iobus.port
    self.bridge.side_b = self.membus.port
    self.physmem.port = self.membus.port
    self.disk0 = CowIdeDisk(driveID='master')
    self.disk2 = CowIdeDisk(driveID='master')
    self.disk0.childImage(mdesc.disk())
    self.disk2.childImage(disk('linux-bigswap2.img'))
    self.tsunami = BaseTsunami()
    self.tsunami.attachIO(self.iobus)
    self.tsunami.ide.pio = self.iobus.port
    self.tsunami.ethernet.pio = self.iobus.port
    self.simple_disk = SimpleDisk(disk=RawDiskImage(image_file = mdesc.disk(),
                                               read_only = True))
    self.intrctrl = IntrControl()
    self.mem_mode = mem_mode
    self.terminal = Terminal()
    self.kernel = binary('vmlinux')
    self.pal = binary('ts_osfpal')
    self.console = binary('console')
    self.boot_osflags = 'root=/dev/hda1 console=ttyS0'

    return self

def makeLinuxAlphaRubySystem(mem_mode, mdesc = None):
    class BaseTsunami(Tsunami):
        ethernet = NSGigE(pci_bus=0, pci_dev=1, pci_func=0)
        ide = IdeController(disks=[Parent.disk0, Parent.disk2],
                            pci_func=0, pci_dev=0, pci_bus=0)
        
    physmem = PhysicalMemory(range = AddrRange(mdesc.mem()))
    self = LinuxAlphaSystem(physmem = physmem)
    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.readfile = mdesc.script()

    # Create pio bus to connect all device pio ports to rubymem's pio port
    self.piobus = Bus(bus_id=0)

    #
    # Pio functional accesses from devices need direct access to memory
    # RubyPort currently does support functional accesses.  Therefore provide
    # the piobus a direct connection to physical memory
    #
    self.piobus.port = physmem.port

    self.disk0 = CowIdeDisk(driveID='master')
    self.disk2 = CowIdeDisk(driveID='master')
    self.disk0.childImage(mdesc.disk())
    self.disk2.childImage(disk('linux-bigswap2.img'))
    self.tsunami = BaseTsunami()
    self.tsunami.attachIO(self.piobus)
    self.tsunami.ide.pio = self.piobus.port
    self.tsunami.ethernet.pio = self.piobus.port

    #
    # store the dma devices for later connection to dma ruby ports
    #
    self.dma_devices = [self.tsunami.ide, self.tsunami.ethernet]

    self.simple_disk = SimpleDisk(disk=RawDiskImage(image_file = mdesc.disk(),
                                               read_only = True))
    self.intrctrl = IntrControl()
    self.mem_mode = mem_mode
    self.terminal = Terminal()
    self.kernel = binary('vmlinux')
    self.pal = binary('ts_osfpal')
    self.console = binary('console')
    self.boot_osflags = 'root=/dev/hda1 console=ttyS0'

    return self

def makeSparcSystem(mem_mode, mdesc = None):
    class CowMmDisk(MmDisk):
        image = CowDiskImage(child=RawDiskImage(read_only=True),
                             read_only=False)

        def childImage(self, ci):
            self.image.child.image_file = ci

    self = SparcSystem()
    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.readfile = mdesc.script()
    self.iobus = Bus(bus_id=0)
    self.membus = MemBus(bus_id=1)
    self.bridge = Bridge(delay='50ns', nack_delay='4ns')
    self.t1000 = T1000()
    self.t1000.attachOnChipIO(self.membus)
    self.t1000.attachIO(self.iobus)
    self.physmem = PhysicalMemory(range = AddrRange(Addr('1MB'), size = '64MB'), zero = True)
    self.physmem2 = PhysicalMemory(range = AddrRange(Addr('2GB'), size ='256MB'), zero = True)
    self.bridge.side_a = self.iobus.port
    self.bridge.side_b = self.membus.port
    self.physmem.port = self.membus.port
    self.physmem2.port = self.membus.port
    self.rom.port = self.membus.port
    self.nvram.port = self.membus.port
    self.hypervisor_desc.port = self.membus.port
    self.partition_desc.port = self.membus.port
    self.intrctrl = IntrControl()
    self.disk0 = CowMmDisk()
    self.disk0.childImage(disk('disk.s10hw2'))
    self.disk0.pio = self.iobus.port
    self.reset_bin = binary('reset_new.bin')
    self.hypervisor_bin = binary('q_new.bin')
    self.openboot_bin = binary('openboot_new.bin')
    self.nvram_bin = binary('nvram1')
    self.hypervisor_desc_bin = binary('1up-hv.bin')
    self.partition_desc_bin = binary('1up-md.bin')

    return self

def makeLinuxMipsSystem(mem_mode, mdesc = None):
    class BaseMalta(Malta):
        ethernet = NSGigE(pci_bus=0, pci_dev=1, pci_func=0)
        ide = IdeController(disks=[Parent.disk0, Parent.disk2],
                            pci_func=0, pci_dev=0, pci_bus=0)

    self = LinuxMipsSystem()
    if not mdesc:
        # generic system
        mdesc = SysConfig()
    self.readfile = mdesc.script()
    self.iobus = Bus(bus_id=0)
    self.membus = MemBus(bus_id=1)
    self.bridge = Bridge(delay='50ns', nack_delay='4ns')
    self.physmem = PhysicalMemory(range = AddrRange('1GB'))
    self.bridge.side_a = self.iobus.port
    self.bridge.side_b = self.membus.port
    self.physmem.port = self.membus.port
    self.disk0 = CowIdeDisk(driveID='master')
    self.disk2 = CowIdeDisk(driveID='master')
    self.disk0.childImage(mdesc.disk())
    self.disk2.childImage(disk('linux-bigswap2.img'))
    self.malta = BaseMalta()
    self.malta.attachIO(self.iobus)
    self.malta.ide.pio = self.iobus.port
    self.malta.ethernet.pio = self.iobus.port
    self.simple_disk = SimpleDisk(disk=RawDiskImage(image_file = mdesc.disk(),
                                               read_only = True))
    self.intrctrl = IntrControl()
    self.mem_mode = mem_mode
    self.terminal = Terminal()
    self.kernel = binary('mips/vmlinux')
    self.console = binary('mips/console')
    self.boot_osflags = 'root=/dev/hda1 console=ttyS0'

    return self

def x86IOAddress(port):
    IO_address_space_base = 0x8000000000000000
    return IO_address_space_base + port;

def makeX86System(mem_mode, numCPUs = 1, mdesc = None, self = None):
    if self == None:
        self = X86System()

    if not mdesc:
        # generic system
        mdesc = SysConfig()
    mdesc.diskname = 'x86root.img'
    self.readfile = mdesc.script()

    self.mem_mode = mem_mode

    # Physical memory
    self.membus = MemBus(bus_id=1)
    self.physmem = PhysicalMemory(range = AddrRange(mdesc.mem()))
    self.physmem.port = self.membus.port

    # North Bridge
    self.iobus = Bus(bus_id=0)
    self.bridge = Bridge(delay='50ns', nack_delay='4ns')
    self.bridge.side_a = self.iobus.port
    self.bridge.side_b = self.membus.port

    # Platform
    self.pc = Pc()
    self.pc.attachIO(self.iobus)

    self.intrctrl = IntrControl()

    # Disks
    disk0 = CowIdeDisk(driveID='master')
    disk2 = CowIdeDisk(driveID='master')
    disk0.childImage(mdesc.disk())
    disk2.childImage(disk('linux-bigswap2.img'))
    self.pc.south_bridge.ide.disks = [disk0, disk2]

    # Add in a Bios information structure.
    structures = [X86SMBiosBiosInformation()]
    self.smbios_table.structures = structures

    # Set up the Intel MP table
    for i in xrange(numCPUs):
        bp = X86IntelMPProcessor(
                local_apic_id = i,
                local_apic_version = 0x14,
                enable = True,
                bootstrap = (i == 0))
        self.intel_mp_table.add_entry(bp)
    io_apic = X86IntelMPIOAPIC(
            id = numCPUs,
            version = 0x11,
            enable = True,
            address = 0xfec00000)
    self.pc.south_bridge.io_apic.apic_id = io_apic.id
    self.intel_mp_table.add_entry(io_apic)
    isa_bus = X86IntelMPBus(bus_id = 0, bus_type='ISA')
    self.intel_mp_table.add_entry(isa_bus)
    pci_bus = X86IntelMPBus(bus_id = 1, bus_type='PCI')
    self.intel_mp_table.add_entry(pci_bus)
    connect_busses = X86IntelMPBusHierarchy(bus_id=0,
            subtractive_decode=True, parent_bus=1)
    self.intel_mp_table.add_entry(connect_busses)
    pci_dev4_inta = X86IntelMPIOIntAssignment(
            interrupt_type = 'INT',
            polarity = 'ConformPolarity',
            trigger = 'ConformTrigger',
            source_bus_id = 1,
            source_bus_irq = 0 + (4 << 2),
            dest_io_apic_id = io_apic.id,
            dest_io_apic_intin = 16)
    self.intel_mp_table.add_entry(pci_dev4_inta);
    def assignISAInt(irq, apicPin):
        assign_8259_to_apic = X86IntelMPIOIntAssignment(
                interrupt_type = 'ExtInt',
                polarity = 'ConformPolarity',
                trigger = 'ConformTrigger',
                source_bus_id = 0,
                source_bus_irq = irq,
                dest_io_apic_id = io_apic.id,
                dest_io_apic_intin = 0)
        self.intel_mp_table.add_entry(assign_8259_to_apic)
        assign_to_apic = X86IntelMPIOIntAssignment(
                interrupt_type = 'INT',
                polarity = 'ConformPolarity',
                trigger = 'ConformTrigger',
                source_bus_id = 0,
                source_bus_irq = irq,
                dest_io_apic_id = io_apic.id,
                dest_io_apic_intin = apicPin)
        self.intel_mp_table.add_entry(assign_to_apic)
    assignISAInt(0, 2)
    assignISAInt(1, 1)
    for i in range(3, 15):
        assignISAInt(i, i)


def makeLinuxX86System(mem_mode, numCPUs = 1, mdesc = None):
    self = LinuxX86System()

    # Build up a generic x86 system and then specialize it for Linux
    makeX86System(mem_mode, numCPUs, mdesc, self)

    # We assume below that there's at least 1MB of memory. We'll require 2
    # just to avoid corner cases.
    assert(self.physmem.range.second.getValue() >= 0x200000)

    # Mark the first megabyte of memory as reserved
    self.e820_table.entries.append(X86E820Entry(
                addr = 0,
                size = '1MB',
                range_type = 2))

    # Mark the rest as available
    self.e820_table.entries.append(X86E820Entry(
                addr = 0x100000,
                size = '%dB' % (self.physmem.range.second - 0x100000 + 1),
                range_type = 1))

    # Command line
    self.boot_osflags = 'earlyprintk=ttyS0 console=ttyS0 lpj=7999923 ' + \
                        'root=/dev/hda1'
    return self


def makeDualRoot(testSystem, driveSystem, dumpfile):
    self = Root()
    self.testsys = testSystem
    self.drivesys = driveSystem
    self.etherlink = EtherLink()
    self.etherlink.int0 = Parent.testsys.tsunami.ethernet.interface
    self.etherlink.int1 = Parent.drivesys.tsunami.ethernet.interface

    if dumpfile:
        self.etherdump = EtherDump(file=dumpfile)
        self.etherlink.dump = Parent.etherdump

    return self

def setMipsOptions(TestCPUClass):
        #CP0 Configuration
        TestCPUClass.CoreParams.CP0_PRId_CompanyOptions = 0
        TestCPUClass.CoreParams.CP0_PRId_CompanyID = 1
        TestCPUClass.CoreParams.CP0_PRId_ProcessorID = 147
        TestCPUClass.CoreParams.CP0_PRId_Revision = 0

        #CP0 Interrupt Control
        TestCPUClass.CoreParams.CP0_IntCtl_IPTI = 7
        TestCPUClass.CoreParams.CP0_IntCtl_IPPCI = 7

        # Config Register
        #TestCPUClass.CoreParams.CP0_Config_K23 = 0 # Since TLB
        #TestCPUClass.CoreParams.CP0_Config_KU = 0 # Since TLB
        TestCPUClass.CoreParams.CP0_Config_BE = 0 # Little Endian
        TestCPUClass.CoreParams.CP0_Config_AR = 1 # Architecture Revision 2
        TestCPUClass.CoreParams.CP0_Config_AT = 0 # MIPS32
        TestCPUClass.CoreParams.CP0_Config_MT = 1 # TLB MMU
        #TestCPUClass.CoreParams.CP0_Config_K0 = 2 # Uncached

        #Config 1 Register
        TestCPUClass.CoreParams.CP0_Config1_M = 1 # Config2 Implemented
        TestCPUClass.CoreParams.CP0_Config1_MMU = 63 # TLB Size
        # ***VERY IMPORTANT***
        # Remember to modify CP0_Config1 according to cache specs
        # Examine file ../common/Cache.py
        TestCPUClass.CoreParams.CP0_Config1_IS = 1 # I-Cache Sets Per Way, 16KB cache, i.e., 1 (128)
        TestCPUClass.CoreParams.CP0_Config1_IL = 5 # I-Cache Line Size, default in Cache.py is 64, i.e 5
        TestCPUClass.CoreParams.CP0_Config1_IA = 1 # I-Cache Associativity, default in Cache.py is 2, i.e, a value of 1
        TestCPUClass.CoreParams.CP0_Config1_DS = 2 # D-Cache Sets Per Way (see below), 32KB cache, i.e., 2
        TestCPUClass.CoreParams.CP0_Config1_DL = 5 # D-Cache Line Size, default is 64, i.e., 5
        TestCPUClass.CoreParams.CP0_Config1_DA = 1 # D-Cache Associativity, default is 2, i.e. 1
        TestCPUClass.CoreParams.CP0_Config1_C2 = 0 # Coprocessor 2 not implemented(?)
        TestCPUClass.CoreParams.CP0_Config1_MD = 0 # MDMX ASE not implemented in Mips32
        TestCPUClass.CoreParams.CP0_Config1_PC = 1 # Performance Counters Implemented
        TestCPUClass.CoreParams.CP0_Config1_WR = 0 # Watch Registers Implemented
        TestCPUClass.CoreParams.CP0_Config1_CA = 0 # Mips16e NOT implemented
        TestCPUClass.CoreParams.CP0_Config1_EP = 0 # EJTag Not Implemented
        TestCPUClass.CoreParams.CP0_Config1_FP = 0 # FPU Implemented

        #Config 2 Register
        TestCPUClass.CoreParams.CP0_Config2_M = 1 # Config3 Implemented
        TestCPUClass.CoreParams.CP0_Config2_TU = 0 # Tertiary Cache Control
        TestCPUClass.CoreParams.CP0_Config2_TS = 0 # Tertiary Cache Sets Per Way
        TestCPUClass.CoreParams.CP0_Config2_TL = 0 # Tertiary Cache Line Size
        TestCPUClass.CoreParams.CP0_Config2_TA = 0 # Tertiary Cache Associativity
        TestCPUClass.CoreParams.CP0_Config2_SU = 0 # Secondary Cache Control
        TestCPUClass.CoreParams.CP0_Config2_SS = 0 # Secondary Cache Sets Per Way
        TestCPUClass.CoreParams.CP0_Config2_SL = 0 # Secondary Cache Line Size
        TestCPUClass.CoreParams.CP0_Config2_SA = 0 # Secondary Cache Associativity


        #Config 3 Register
        TestCPUClass.CoreParams.CP0_Config3_M = 0 # Config4 Not Implemented
        TestCPUClass.CoreParams.CP0_Config3_DSPP = 1 # DSP ASE Present
        TestCPUClass.CoreParams.CP0_Config3_LPA = 0 # Large Physical Addresses Not supported in Mips32
        TestCPUClass.CoreParams.CP0_Config3_VEIC = 0 # EIC Supported
        TestCPUClass.CoreParams.CP0_Config3_VInt = 0 # Vectored Interrupts Implemented
        TestCPUClass.CoreParams.CP0_Config3_SP = 0 # Small Pages Supported (PageGrain reg. exists)
        TestCPUClass.CoreParams.CP0_Config3_MT = 0 # MT Not present
        TestCPUClass.CoreParams.CP0_Config3_SM = 0 # SmartMIPS ASE Not implemented
        TestCPUClass.CoreParams.CP0_Config3_TL = 0 # TraceLogic Not implemented

        #SRS Ctl - HSS
        TestCPUClass.CoreParams.CP0_SrsCtl_HSS = 3 # Four shadow register sets implemented


        #TestCPUClass.CoreParams.tlb = TLB()
        #TestCPUClass.CoreParams.UnifiedTLB = 1
