#!/usr/bin/env python3
"""Instruction-level simulator for the firmwareV2 ARM7TDMI ELF (issue #96).

QEMU has no OKI ML67Q4051 machine model and our memory map does not fit any
stock QEMU board, so we drive the CPU core directly with the Unicorn Engine and
model the memory map + peripheral stubs ourselves.

What it does:
  * maps the real memory regions (internal flash, internal RAM, external RAM) at
    their true addresses and loads the ELF's LOAD segments;
  * starts execution at the ELF entry (Reset_Handler);
  * lazily maps peripheral pages as zero-filled RAM so MMIO accesses succeed,
    and logs writes to them (decoding the LED GPIO lines for readability);
  * implements ARM semihosting (SYS_WRITE0/WRITEC/READC) on the SWI vector so a
    future console/REPL (M3/M4) can run with no hardware;
  * reports whether execution reached main() and exits non-zero if not.

What it does NOT do: model timing, or any peripheral it does not stub (audio,
WiFi, RFID, real timers). It validates code paths + GPIO + console, not analog
behaviour. See src/firmwareV2/README.md.
"""
import argparse
import sys

from unicorn import (
    Uc, UcError,
    UC_ARCH_ARM, UC_MODE_ARM,
    UC_HOOK_CODE, UC_HOOK_INTR,
    UC_HOOK_MEM_WRITE,
    UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED,
    UC_HOOK_MEM_FETCH_UNMAPPED,
)
from unicorn.arm_const import (
    UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR,
    UC_ARM_REG_R0, UC_ARM_REG_R1,
)
from elftools.elf.elffile import ELFFile

PAGE = 0x1000


def align_down(x):
    return x & ~(PAGE - 1)


def align_up(x):
    return (x + PAGE - 1) & ~(PAGE - 1)


# --- ML67Q4051 memory map (from sys/ml67q4051.ld + sys/inc/ml674061.h) ---------
REGIONS = [
    ("IntFlash", 0x08000000, 0x00020000),   # 128 KB internal flash
    ("IntRAM",   0x10000000, 0x00004000),   # 16 KB internal RAM
    ("ExtRAM",   0xD0000000, 0x00100000),   # 1 MB external RAM
]

# Address windows that are memory-mapped peripherals: accesses here are stubbed
# (mapped lazily, reads return last-written/0) rather than treated as faults.
PERIPH_WINDOWS = [
    (0x78000000, 0x7C000000),   # ICR, EMC, DMA, EIC
    (0xB6000000, 0xB9000000),   # ADC, clock/GPCTL, GPIO ports, SPI, I2C/I2S, RTC, WDT, timers
    (0xF0000000, 0xF1000000),   # USB device controller (ML60842)
]

# SPI0/SPI1: after each byte, the firmware busy-waits on the SPIF (transfer-
# complete) bit of the status register SPSR<n> before proceeding. We model no SPI
# engine and no timing, so a transfer "completes" instantly: a write to the data
# register SPDWR<n> sets SPIF in SPSR<n>, releasing that busy-wait. Without this,
# any SPI user (LED driver, flash, audio) spins forever on a flag that never sets.
#   SPI0_BASE = 0xB7B02000, SPI1_BASE = 0xB7B03000; SPSR = base+0x08, SPDWR = +0x0C.
SPI_SPIF = 0x20
SPI_DATA_TO_STATUS = {0xB7B0200C: 0xB7B02008, 0xB7B0300C: 0xB7B03008}

# Named registers worth decoding in the write log (esp. the LED lines).
#   PCR_BASE0 = 0xB7A00000, stride 0x1000; PO<n> = base+0. PCB_RELEASE == LLC2_4c.
PO2, PO4 = 0xB7A02000, 0xB7A04000
NAMED_REGS = {
    0xB7000010: "CLKCNT",
    0x78100000: "BWC", 0x78100008: "RAMAC", 0x7810000C: "IO0AC",
    PO2: "PO2 (MODE_LED=bit7)",
    PO4: "PO4 (CS_LED=bit5, CS_AUDIO_AMP=bit6)",
    0xB7B0200C: "SPDWR0 (SPI0 data)", 0xB7B0300C: "SPDWR1 (SPI1 = LED bus)",
}


def in_periph(addr):
    return any(lo <= addr < hi for lo, hi in PERIPH_WINDOWS)


class Sim:
    def __init__(self, elf_path, budget, verbose, stdin=b""):
        self.budget = budget
        self.verbose = verbose
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self._periph_pages = set()
        self.periph_writes = 0
        self.reached_main = False
        self.console = bytearray()   # semihosting output
        self.stdin = stdin           # bytes fed to SYS_READC (console input)
        self.stdin_pos = 0
        self.entry = None
        self.main_addr = None
        self._load(elf_path)
        self._hook()

    # -- ELF load ---------------------------------------------------------------
    def _load(self, path):
        for name, base, size in REGIONS:
            self.uc.mem_map(base, size)
        with open(path, "rb") as f:
            elf = ELFFile(f)
            self.entry = elf.header.e_entry
            for seg in elf.iter_segments():
                if seg["p_type"] != "PT_LOAD":
                    continue
                # Load each segment's file bytes at its PHYSICAL (load) address,
                # exactly like a flash programmer writes the ROM image - NOT at the
                # virtual address. They differ for the .data segment (LMA in flash,
                # VMA in RAM): init.s copies .data LMA->VMA and zeroes .bss at boot.
                # Writing at the VMA instead would leave the flash LMA blank, so
                # that boot-time copy would clobber .data with zeros (e.g. newlib's
                # _impure_ptr -> NULL, crashing the first stdio/malloc call).
                paddr, data = seg["p_paddr"], seg.data()
                if data:
                    self.uc.mem_write(paddr, data)
            symtab = elf.get_section_by_name(".symtab")
            if symtab:
                syms = symtab.get_symbol_by_name("main")
                if syms:
                    self.main_addr = syms[0]["st_value"] & ~1   # strip Thumb bit

    # -- hooks ------------------------------------------------------------------
    def _hook(self):
        u = self.uc
        u.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                   | UC_HOOK_MEM_FETCH_UNMAPPED, self._on_unmapped)
        u.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        u.hook_add(UC_HOOK_INTR, self._on_intr)
        if self.main_addr is not None:
            u.hook_add(UC_HOOK_CODE, self._on_main,
                       begin=self.main_addr, end=self.main_addr + 2)

    def _on_unmapped(self, uc, access, address, size, value, _ud):
        """Lazily map peripheral pages; a genuine bad access aborts the run."""
        if not in_periph(address):
            print(f"  !! FAULT: unmapped access @ 0x{address:08x} "
                  f"(size {size}) - not a modelled region", flush=True)
            return False   # unhandled -> emulation stops
        page = align_down(address)
        if page not in self._periph_pages:
            uc.mem_map(page, PAGE)
            self._periph_pages.add(page)
        return True        # mapped now -> retry the access

    def _on_write(self, uc, _access, address, size, value, _ud):
        if not in_periph(address):
            return
        self.periph_writes += 1
        if self.verbose:
            name = NAMED_REGS.get(address, "")
            tag = f" {name}" if name else ""
            print(f"  MMIO w 0x{address:08x} <- 0x{value:0{size * 2}x}{tag}",
                  flush=True)
        # Instant SPI completion: writing a data register sets SPIF in its status
        # register so the firmware's transfer-complete poll returns immediately.
        status = SPI_DATA_TO_STATUS.get(address)
        if status is not None:
            cur = int.from_bytes(uc.mem_read(status, 4), "little")
            uc.mem_write(status, (cur | SPI_SPIF).to_bytes(4, "little"))

    def _on_intr(self, uc, intno, _ud):
        """ARM semihosting on SWI. Exercised from M3; harmless before then."""
        cpsr = uc.reg_read(UC_ARM_REG_CPSR)
        thumb = bool(cpsr & 0x20)
        pc = uc.reg_read(UC_ARM_REG_PC)
        # Recover the SWI immediate to confirm it is a semihosting call.
        if thumb:
            instr = int.from_bytes(uc.mem_read(pc - 2, 2), "little")
            swi = instr & 0xFF
            is_semi = swi == 0xAB
        else:
            instr = int.from_bytes(uc.mem_read(pc - 4, 4), "little")
            swi = instr & 0xFFFFFF
            is_semi = swi == 0x123456
        if not is_semi:
            return   # not semihosting (e.g. the firmware's own SWI IRQ macros)
        op = uc.reg_read(UC_ARM_REG_R0)
        arg = uc.reg_read(UC_ARM_REG_R1)
        if op == 0x03:                                   # SYS_WRITEC
            self.console += uc.mem_read(arg, 1)
        elif op == 0x04:                                 # SYS_WRITE0
            s = bytearray()
            while True:
                b = uc.mem_read(arg + len(s), 1)
                if b == b"\x00":
                    break
                s += b
            self.console += s
        elif op == 0x07:                                 # SYS_READC
            # Feed --input bytes one at a time; 0 once exhausted (console EOF).
            if self.stdin_pos < len(self.stdin):
                ch = self.stdin[self.stdin_pos]
                self.stdin_pos += 1
            else:
                ch = 0
            uc.reg_write(UC_ARM_REG_R0, ch)

    def _on_main(self, uc, address, size, _ud):
        if not self.reached_main:
            self.reached_main = True
            print(f"  -> reached main() @ 0x{address:08x}", flush=True)

    # -- run --------------------------------------------------------------------
    def run(self):
        u = self.uc
        print(f"entry = 0x{self.entry:08x}"
              + (f", main = 0x{self.main_addr:08x}" if self.main_addr else "")
              + f", budget = {self.budget} insns")
        try:
            u.emu_start(self.entry, 0xFFFFFFF0, count=self.budget)
        except UcError as e:
            print(f"  emulation stopped: {e}", flush=True)
        pc = u.reg_read(UC_ARM_REG_PC)
        print("--- result ---")
        print(f"  final PC        = 0x{pc:08x}")
        print(f"  reached main    = {self.reached_main}")
        print(f"  peripheral wr   = {self.periph_writes}")
        if self.console:
            print(f"  semihost output = {self.console.decode('latin1')!r}")
        return 0 if self.reached_main else 1


def main():
    ap = argparse.ArgumentParser(description="Unicorn simulator for firmwareV2 ELFs")
    ap.add_argument("elf", help="path to the ELF to run (e.g. bin/hello.elf)")
    ap.add_argument("-n", "--budget", type=int, default=300_000,
                    help="max instructions to execute (default 300000)")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="log every peripheral (MMIO) write")
    ap.add_argument("-i", "--input", default="",
                    help=r"console input fed to SYS_READC (e.g. 'print(2+2)\n'); "
                         r"backslash escapes like \n and \t are interpreted")
    args = ap.parse_args()
    stdin = args.input.encode("latin1").decode("unicode_escape").encode("latin1")
    sys.exit(Sim(args.elf, args.budget, args.verbose, stdin).run())


if __name__ == "__main__":
    main()
