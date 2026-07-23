#!/usr/bin/env python3
"""Instruction-level simulator for the lua-firmware ARM7TDMI ELF (issue #96).

QEMU has no OKI ML67Q4051 machine model and our memory map does not fit any
stock QEMU board, so we drive the CPU core directly with the Unicorn Engine and
model the memory map + peripheral stubs ourselves.

What it does:
  * maps the real memory regions (internal flash, internal RAM, external RAM) at
    their true addresses and loads the ELF's LOAD segments;
  * starts execution at the ELF entry (Reset_Handler);
  * lazily maps peripheral pages as zero-filled RAM so MMIO accesses succeed,
    and logs writes to them (decoding the LED GPIO lines for readability);
  * models the UART0 console (TX/RX) so the Lua REPL runs with no hardware
    (#207); see the UART0 block below;
  * delivers the 1 ms System Timer IRQ between instruction slices so the LED
    fade engine advances, and can reconstruct + draw the five RGB LEDs (#102);
  * models the input peripherals (#42) - button, RFID, ear drive + encoder -
    driven from a JSON-Lines control timeline (--inject-file) and reports device
    state on change (--emit-state); see the "peripheral I/O" block below;
  * reports whether execution reached main() and exits non-zero if not.

What it does NOT do: model timing beyond the 1 ms tick, or any peripheral it
does not stub beyond instant completion (audio, WiFi). It validates code paths +
GPIO + SPI/I2C framing + console, not analog behaviour. See lua/firmware/README.md.
"""
import argparse
import json
import signal
import sys
import time

from unicorn import (
    Uc, UcError,
    UC_ARCH_ARM, UC_MODE_ARM,
    UC_HOOK_CODE,
    UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,
    UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED,
    UC_HOOK_MEM_FETCH_UNMAPPED,
)
from unicorn.arm_const import (
    UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR,
    UC_ARM_REG_SPSR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
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

# I2C (M9 #117): hal/i2c.c busy-waits on the MCF (transfer-complete) bit of
# I2CSR - set after writing I2CCTL/I2CDR, and (in read_i2c's receive loop) after
# software clears I2CSR to wait for the next byte, which real hardware then
# re-asserts on its own with no further software write. A write-triggered stub
# (like the SPI SPIF one below) cannot model that last case: the loop's own
# `put_hvalue(I2CSR, 0)` writes the SAME register our stub would need to set
# MCF back on, and Unicorn's MEM_WRITE hook fires *before* the store commits -
# any fix-up written from inside the hook is clobbered when the pending store
# lands right after (confirmed with a standalone Unicorn repro; the SPI stub
# never hit this because it always writes a *different* register than the one
# it fixes up). So instead we hook *reads* of I2CSR directly and force the
# value the CPU sees to always read as "instantly complete, no error": MCF set,
# RXAK/MBB/MAL clear (MEM_READ hooks fire *before* the load, so an in-hook
# write is visible to that same read - also repro-confirmed). This lets I2C
# bring-up and the CRX14 command sequence run to completion without a real bus
# or coupler model; RFID reads get back the last address byte written to
# I2CDR, not real tag data (no CRX14 emulation), same caveat as audio/SPI.
#   I2C_BASE = 0xB7800C00; I2CCTL = +0x04, I2CSR = +0x08 (halfword), I2CDR = +0x0C.
I2C_MCF = 0x0080
I2CSR_ADDR = 0xB7800C08

# Input peripherals modelled at the register level (#42), same approach as the
# SPI/I2C/UART stubs: a MEM_READ hook forces the value the CPU sees.
#   button_pressed() reads PI3 bit1 (active-low, PCR_BASE3+0x04): pressed=0.
#   get_motor_position(n) reads the FTM capture counter - FTM0C (motor 1) /
#   FTM1C (motor 2) - as a halfword; we return the synthetic encoder count.
PI3_ADDR   = 0xB7A03004  # port 3 input register (button on bit1, active-low)
BUTTON_BIT = 0x02        # PI3 bit1 = INT_SWITCH (head button)
FTM0C_ADDR = 0xB7F00008  # ear motor 1 encoder capture counter (halfword)
FTM1C_ADDR = 0xB7F00028  # ear motor 2 encoder capture counter (halfword)

# UART0 console model (#207): the firmware's Lua REPL does polled TX/RX on the
# 16550-style UART0 (hal/uart.c) - it is the console. We model just enough to
# carry it: a THR write is a console-out byte, an RBR read
# delivers the next input byte, and LSR reports TX-always-ready plus a data-ready
# (DR) bit driven by the input buffer. DLAB (LCR bit 7) must be tracked because
# THR/RBR and the baud divisor latch (DLL) share offset +0x00 - the divisor bytes
# written during init_uart() must not be mistaken for console output.
#   UCR_BASE = 0xB7B00000; RBR/THR/DLL = +0x00, LCR = +0x0C, LSR = +0x14.
UART0_THR = 0xB7B00000   # RBR (read) / THR (write) / DLL (write, DLAB=1)
UART0_LCR = 0xB7B0000C
UART0_LSR = 0xB7B00014
UART_DLAB = 0x80         # LCR: divisor-latch access
UART_LSR_DR = 0x01       # LSR: RX data ready
UART_LSR_TXRDY = 0x60    # LSR: THRE|TEMT - TX holding+shift empty (always, no timing)

# Named registers worth decoding in the write log (esp. the LED lines).
#   PCR_BASE0 = 0xB7A00000, stride 0x1000; PO<n> = base+0. PCB_RELEASE == LLC2_4c.
PO2, PO4 = 0xB7A02000, 0xB7A04000
NAMED_REGS = {
    0xB7000010: "CLKCNT",
    0x78100000: "BWC", 0x78100008: "RAMAC", 0x7810000C: "IO0AC",
    PO2: "PO2 (MODE_LED=bit7)",
    PO4: "PO4 (CS_LED=bit5, CS_AUDIO_AMP=bit6)",
    0xB7B0200C: "SPDWR0 (SPI0 data)", 0xB7B0300C: "SPDWR1 (SPI1 = LED bus)",
    0xB7800C04: "I2CCTL", 0xB7800C0C: "I2CDR",
}


# --- System Timer + IRQ delivery (issue #102) ---------------------------------
# The real chip's OKI System Timer raises an IRQ every 1 ms; its handler bumps
# counter_timer and ticks the LED fade engine (led_fade_tick). Unicorn does not
# model timers or deliver interrupts, so we do it ourselves: run the CPU in ~1 ms
# instruction slices and, between slices, perform a real ARM7 IRQ entry (vector
# 0x18) when the timer is enabled and interrupts are unmasked. The firmware's own
# reentrant dispatcher then runs timer_handler - the actual fade code, driving
# the LED bus exactly as on hardware. Without this the fades never advance.
TMEN_ADDR  = 0xB8001004   # System Timer enable (nonzero = running)
IRN_ADDR   = 0x78000014   # interrupt-number register the dispatcher reads
INT_SYSTEM_TIMER = 0      # its value for the System Timer (sys/inc/ml674061.h)
# ARM IRQ vector. The core fetches exceptions from 0x18, but this chip aliases
# flash there (reset boots from flash); the vector table + literal pool live in
# the flash image at IntFlash base, so we vector to the flash-resident copy -
# `ldr pc,=irq_handler`, whose PC-relative literal is right beside it in flash.
IRQ_VECTOR = 0x08000000 + 0x18
# Sim instructions taken to stand for 1 ms, and the emulation slice size. Tuned
# so counter_timer advances at roughly the rate nab.delay()'s busy-loop treats
# as 1 ms, keeping fades and delays on the same clock. Approximate by nature.
INSNS_PER_MS = 30000

# LED framing: the driver shifts its 14-byte dot-correction image out on SPI1
# inside a single CS_LED-low window with MODE_LED in dot-correction (PO2 bit7=1);
# we latch that frame on the CS_LED rising edge and un-pack it to five RGB LEDs.
PO2_ADDR, PO4_ADDR = 0xB7A02000, 0xB7A04000
MODE_LED_BIT = 0x80    # PO2 bit7
CS_LED_BIT   = 0x20    # PO4 bit5
SPDWR1_ADDR  = 0xB7B0300C
# physical LED index 0..4 -> name (inc/hal/led.h, verified on LLC2_4c)
LED_PHYS_NAME = ["belly", "bottom", "left", "right", "nose"]
# order + rough geometry for the on-screen strip (a little rabbit face)
LED_VIEW = [("nose", 4), ("belly", 0), ("left", 2), ("right", 3), ("bottom", 1)]


# Pages the write hook actually acts on (SPI completion stub, UART console, LED
# GPIO framing, timer enable). Scoping to these keeps a Python callback from
# firing on *every* store - nab.delay's busy-loop hammers WDTCON (0xB7E0_0000)
# millions of times a second, which otherwise makes long animated runs (the LED
# demo) crawl.
WRITE_HOOK_RANGES = [
    (0xB7A00000, 0xB7A0FFFF),   # PCR GPIO ports: PO2 (MODE_LED), PO4 (CS_LED)
    (0xB7B00000, 0xB7B0FFFF),   # UART0 console + SPI0/SPI1 data/status (LED bus)
    (0xB8001000, 0xB8001FFF),   # System Timer control (TMEN)
]


def in_periph(addr):
    return any(lo <= addr < hi for lo, hi in PERIPH_WINDOWS)


def unpack_dotcorr(buf):
    """Un-pack a 14-byte TLC5922 dot-correction image to 5 x (r,g,b) 7-bit."""
    out = []
    for p in range(5):
        ch = []
        for k in range(3):
            c = p * 3 + k
            byte = (c * 7) >> 3
            off = (c * 7) & 7
            ch.append(((buf[byte] << 8 | buf[byte + 1]) >> (9 - off)) & 0x7F)
        out.append(tuple(ch))
    return out


class Sim:
    def __init__(self, elf_path, budget, verbose, stdin=b"", interactive=False,
                 console_only=False, show_leds=False, speed=None,
                 inject_events=None, emit_state=False, state_file=None):
        self.budget = budget
        self.verbose = verbose
        # Wall-clock pacing (see run()): None = as fast as possible; a float is a
        # real-time multiplier (1.0 = real time, 0.25 = quarter speed). Without it
        # an animation blows past in a host-CPU instant - too fast to watch (#102).
        self.speed = speed
        self.console_only = console_only   # batch: stdout = raw transcript only
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self._periph_pages = set()
        self.periph_writes = 0
        self.reached_main = False
        self.stopped = False         # set when a hook/signal asks emulation to stop
        self.console = bytearray()   # console output (UART THR)
        self.stdin = stdin           # bytes fed to the console (UART RBR)
        self.stdin_pos = 0
        self.dlab = False            # UART LCR divisor-latch-access bit
        self._rx = None              # interactive: one prefetched input byte
        self._uart_eot_sent = False  # batch: EOT appended once input is exhausted
        self.getch_addr = None       # getch_uart entry, for the RX code hook
        self.rxrdy_addr = None       # rxrdy_uart entry, for the RX-peek code hook
        self.waiti2c_addr = None     # waiti2cmcf entry, for the I2C-done code hook
        # System Timer / IRQ state (#102)
        self.timer_enabled = False
        self.timer_pending = 0       # ms ticks owed while interrupts were masked
        self.timer_ticks = 0         # IRQs actually delivered
        # LED frame reconstruction + view (#102)
        self.show_leds = show_leds
        self.leds_tty = show_leds and sys.stdout.isatty()
        self.cs_led = True           # CS_LED idles high
        self.mode_led_dc = False     # MODE_LED in dot-correction?
        self.led_buf = bytearray()   # SPI1 bytes of the current frame
        self.led_rgb = [(0, 0, 0)] * 5
        self.led_frames = 0
        self._led_last = None
        self._leds_drawn = False
        # Input/output peripheral model (#42). The sim already observes the LED
        # bus (above); these model the peripherals the firmware *reads* (button,
        # RFID) and the ear motors it drives, so scripts/UI can inject inputs and
        # observe device state. State is driven through symbol-entry hooks on the
        # real HAL functions (resolved in _load), not extra MMIO decoding.
        self.button = False              # nab.button() -> button_pressed()
        self.rfid_uid = None             # nab.rfid() UID hex string, or None
        # ear[0]=motor 1, ear[1]=motor 2; pos is the synthetic 16-bit encoder.
        self.ears = [{"dir": "forward", "run": False, "pos": 0},
                     {"dir": "forward", "run": False, "pos": 0}]
        self.encoder_rate = 8            # synthetic encoder counts per ms while running
        self.rfid_addr = None            # rfid_read_uid entry (capture out buffer)
        self.runmotor_addr = None        # run_motor entry (capture args)
        self.stopmotor_addr = None       # stop_motor entry (capture args)
        self._rfid_buf = None            # rfid_read_uid's uid_out ptr (r0 at entry)
        self._rfid_ret_hook = None       # transient hook at its return site
        # Peripheral control protocol (JSON-Lines, #42). inject_events is a list
        # of {t,...} control objects each optionally gated by at_ms/after; they
        # are applied between instruction slices (see _apply_injections). State
        # snapshots are emitted on change when emit_state is set.
        self.inject_events = inject_events or []
        self.emit_state = emit_state
        self.state_file = state_file     # file object for state JSON, else stderr
        self._last_state_key = None
        self.elapsed_ms = 0              # slice-based device-ms clock for gating
        # Interactive: read input live from the real terminal and echo the
        # console straight to stdout, so you can type at the REPL.
        self.interactive = interactive
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
                gu = symtab.get_symbol_by_name("getch_uart")
                if gu:
                    self.getch_addr = gu[0]["st_value"] & ~1    # strip Thumb bit
                ru = symtab.get_symbol_by_name("rxrdy_uart")
                if ru:
                    self.rxrdy_addr = ru[0]["st_value"] & ~1    # strip Thumb bit
                wi = symtab.get_symbol_by_name("waiti2cmcf")
                if wi:
                    self.waiti2c_addr = wi[0]["st_value"] & ~1  # strip Thumb bit
                # HAL peripheral seam (#42): hook these firmware functions at
                # entry to inject inputs / observe ear drive. Same symbol-hook
                # approach as getch_uart above; Thumb bit stripped for the hook.
                def _sym(name):
                    s = symtab.get_symbol_by_name(name)
                    return (s[0]["st_value"] & ~1) if s else None
                # button + ear position are modelled at the register level (PI3 /
                # FTM read hooks) - no symbol needed. rfid_read_uid runs a whole
                # CRX14 I2C transaction (register-level would mean emulating the
                # coupler, out of scope), so it is skipped at the symbol; run/stop
                # _motor are observed at entry for the ear drive state.
                self.rfid_addr = _sym("rfid_read_uid")
                self.runmotor_addr = _sym("run_motor")
                self.stopmotor_addr = _sym("stop_motor")

    # -- hooks ------------------------------------------------------------------
    def _hook(self):
        u = self.uc
        u.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED
                   | UC_HOOK_MEM_FETCH_UNMAPPED, self._on_unmapped)
        # Verbose mode logs every MMIO write, so it needs the global hook;
        # otherwise scope the write hook to the pages we act on (huge speedup -
        # see WRITE_HOOK_RANGES) so busy-loop WDTCON stores don't call Python.
        if self.verbose:
            u.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        else:
            for lo, hi in WRITE_HOOK_RANGES:
                u.hook_add(UC_HOOK_MEM_WRITE, self._on_write, begin=lo, end=hi)
        # Scope the read hook to just the pages it fixes up - the I2CSR register
        # and the UART0 LSR. Firing a Python callback on *every* memory read
        # (operand loads included) is the sim's main slowdown, and restricting it
        # keeps long animated runs (the #102 LED demo) watchable.
        for base in (align_down(I2CSR_ADDR), align_down(UART0_LSR)):
            u.hook_add(UC_HOOK_MEM_READ, self._on_read, begin=base, end=base + PAGE - 1)
        # Input peripherals (#42): force the button (PI3) and ear-encoder (FTM
        # capture) reads from device state, same read-hook fixup pattern as I2CSR
        # /LSR above. PI3 shares the PCR page block; FTM has its own page.
        for base in (align_down(PI3_ADDR), align_down(FTM0C_ADDR)):
            u.hook_add(UC_HOOK_MEM_READ, self._on_input_read,
                       begin=base, end=base + PAGE - 1)
        if self.main_addr is not None:
            u.hook_add(UC_HOOK_CODE, self._on_main,
                       begin=self.main_addr, end=self.main_addr + 2)
        if self.getch_addr is not None:
            # end = begin: fire ONLY on the entry instruction. A wider range spans
            # the next Thumb instruction too (the ldrb that reads LSR), firing the
            # hook twice per call - the second fire clears DR before the read.
            u.hook_add(UC_HOOK_CODE, self._on_getch,
                       begin=self.getch_addr, end=self.getch_addr)
        if self.rxrdy_addr is not None:
            u.hook_add(UC_HOOK_CODE, self._on_rxrdy,
                       begin=self.rxrdy_addr, end=self.rxrdy_addr)
        if self.waiti2c_addr is not None:
            u.hook_add(UC_HOOK_CODE, self._on_waiti2c,
                       begin=self.waiti2c_addr, end=self.waiti2c_addr)
        # Peripheral seam (#42): entry hooks on the HAL functions. rfid_read_uid
        # is skipped (its CRX14 I2C body can't complete in-sim) and its result
        # injected; run/stop_motor only capture their args (the real body runs
        # on against the stubbed PWM regs, harmless). end=begin: fire on the
        # entry insn only, where r0..r3 still hold the call arguments.
        for addr, cb in ((self.rfid_addr, self._on_rfid_read),
                         (self.runmotor_addr, self._on_run_motor),
                         (self.stopmotor_addr, self._on_stop_motor)):
            if addr is not None:
                u.hook_add(UC_HOOK_CODE, cb, begin=addr, end=addr)

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
        # UART0 console: track DLAB, and treat a THR write (DLAB clear) as one
        # console-out byte. THR writes under DLAB are the baud divisor - skip them.
        elif address == UART0_LCR:
            self.dlab = bool(value & UART_DLAB)
        elif address == UART0_THR and not self.dlab:
            self._console_out(bytes([value & 0xFF]))

        # System Timer enable + LED-bus framing (#102). Separate if-chain: a
        # SPDWR1 write both completes the SPI byte (above) and feeds the LED frame.
        if address == TMEN_ADDR:
            self.timer_enabled = (value != 0)
        elif address == PO2_ADDR:
            self.mode_led_dc = bool(value & MODE_LED_BIT)
        elif address == PO4_ADDR:
            new_cs = bool(value & CS_LED_BIT)
            if self.cs_led and not new_cs:          # falling edge: frame starts
                self.led_buf = bytearray()
            elif (not self.cs_led) and new_cs:      # rising edge: frame complete
                self._latch_led_frame()
            self.cs_led = new_cs
        elif address == SPDWR1_ADDR and not self.cs_led:
            self.led_buf.append(value & 0xFF)

    def _on_read(self, uc, _access, address, size, _value, _ud):
        # Instant I2C completion: force I2CSR to always read as "transfer done,
        # no error" (see the I2C_MCF comment above for why this is a read hook,
        # not a write-triggered one like the SPI stub).
        if address == I2CSR_ADDR:
            uc.mem_write(I2CSR_ADDR, I2C_MCF.to_bytes(2, "little"))
        # UART0 TX is always ready: OR the TX-ready bits into LSR for putch_uart's
        # THRE poll. The RX data-ready (DR) bit and the RBR byte are owned by the
        # getch_uart code hook (_on_getch), not serviced here - Unicorn does not
        # reliably re-fire a MEM_READ hook for getch_uart's tight same-address
        # polling loop, so the read-hook approach left it reading stale registers.
        elif address == UART0_LSR:
            cur = int.from_bytes(uc.mem_read(UART0_LSR, 4), "little")
            uc.mem_write(UART0_LSR, (cur | UART_LSR_TXRDY).to_bytes(4, "little"))

    # -- LED view (#102) --------------------------------------------------------
    def _latch_led_frame(self):
        """A CS-framed SPI1 burst just finished; if it was a dot-correction
        image, un-pack it to five RGB LEDs and (optionally) draw them."""
        if not self.mode_led_dc or len(self.led_buf) < 14:
            return
        self.led_rgb = unpack_dotcorr(self.led_buf[-14:])
        self.led_frames += 1
        if self.show_leds:
            self._render_leds()

    def _render_leds(self):
        def swatch(rgb):
            r, g, b = (v * 255 // 127 for v in rgb)
            return f"\x1b[38;2;{r};{g};{b}m●\x1b[0m"
        cells = [f"{swatch(self.led_rgb[p])} {name}" for name, p in LED_VIEW]
        line = "  ".join(cells)
        if self.leds_tty:
            # redraw the strip in place
            if self._leds_drawn:
                sys.stdout.write("\r\x1b[2K")
            sys.stdout.write(" LEDs: " + line)
            sys.stdout.flush()
            self._leds_drawn = True
        else:
            # batch mode: one line per *distinct* frame, so a fade is a readable
            # sequence instead of thousands of identical rows
            key = tuple(self.led_rgb)
            if key != self._led_last:
                self._led_last = key
                print(" LEDs: " + line, flush=True)

    def _fire_timer_irq(self):
        """Deliver one System Timer IRQ if interrupts are unmasked. Returns True
        if the CPU was vectored into the handler (PC now at 0x18)."""
        uc = self.uc
        cpsr = uc.reg_read(UC_ARM_REG_CPSR)
        if cpsr & 0x80:                 # I-bit set: IRQs masked, keep it pending
            return False
        pc = uc.reg_read(UC_ARM_REG_PC)
        # the dispatcher reads IRN for the interrupt number
        page = align_down(IRN_ADDR)
        if page not in self._periph_pages:
            uc.mem_map(page, PAGE)
            self._periph_pages.add(page)
        uc.mem_write(IRN_ADDR, INT_SYSTEM_TIMER.to_bytes(4, "little"))
        # ARM7 IRQ entry: SPSR_irq = CPSR; switch to IRQ mode (ARM, I=1);
        # LR_irq = return address + 4 (the handler does `sub lr,#4`).
        new_cpsr = (cpsr & ~0x1F & ~0x20) | 0x12 | 0x80
        uc.reg_write(UC_ARM_REG_CPSR, new_cpsr)
        uc.reg_write(UC_ARM_REG_SPSR, cpsr)
        uc.reg_write(UC_ARM_REG_LR, (pc + 4) & 0xFFFFFFFF)
        uc.reg_write(UC_ARM_REG_PC, IRQ_VECTOR)
        self.timer_ticks += 1
        return True

    def _console_out(self, data):
        """Console write (UART THR): echo live if interactive, else
        buffer. Stop as soon as the firmware prints its done-marker so batch and
        interactive runs both end promptly instead of spinning to the budget."""
        self.console += data
        if self.interactive:
            sys.stdout.buffer.write(data)
            sys.stdout.flush()
        if self.console.endswith(b"<<FV_DONE>>\n"):
            self.stopped = True
            self.uc.emu_stop()

    # -- UART0 RX (console input): the polled counterpart of _console_out --------
    def _uart_rx_ready(self):
        """Whether getch_uart() should see a byte now (drives the LSR DR bit)."""
        if self.interactive:
            if self._rx is None:
                self._rx = self._stdin_byte_blocking()
            return True
        # Batch: real input, then a single synthetic EOT so the REPL hits EOF.
        return self.stdin_pos < len(self.stdin) or not self._uart_eot_sent

    def _uart_rx_take(self):
        """Consume and return one RX byte (an RBR read)."""
        if self.interactive:
            b = self._rx if self._rx is not None else self._stdin_byte_blocking()
            self._rx = None
            return b
        if self.stdin_pos < len(self.stdin):
            ch = self.stdin[self.stdin_pos]
            self.stdin_pos += 1
            return ch
        self._uart_eot_sent = True
        return 0x04   # EOT: batch input exhausted -> _read() returns EOF

    def _stdin_byte_blocking(self):
        """Block for one byte of live terminal input; EOF (Ctrl-D) -> EOT."""
        b = sys.stdin.buffer.read(1)
        return b[0] if b else 0x04

    def _on_waiti2c(self, uc, address, size, _ud):
        """Force I2CSR = "transfer complete" in memory before waiti2cmcf polls it.
        The MEM_READ hook (_on_read) also does this, but Unicorn does not reliably
        re-fire read hooks inside the tight MCF-poll loop that nab.rfid()'s CRX14
        retries drive (init_i2c's spread-out waits are fine) - so seed it here at
        the function entry, exactly like the getch_uart RX hook. waiti2cmcf exits
        on the first read once MCF is set, so seeding once at entry is enough."""
        uc.mem_write(I2CSR_ADDR, I2C_MCF.to_bytes(2, "little"))

    # -- peripheral seam (#42): inject inputs / observe ear drive --------------
    def _on_input_read(self, uc, _access, address, size, _value, _ud):
        """Force the button / ear-encoder reads from device state, before the
        CPU's load commits (same as the I2CSR/LSR read fixups). PI3 bit1 is the
        head button (active-low: 0 = pressed); FTM0C/FTM1C are the ear encoder
        capture counters that get_motor_position reads as a halfword."""
        if address == PI3_ADDR:
            cur = uc.mem_read(PI3_ADDR, 1)[0]
            cur = (cur & ~BUTTON_BIT) | (0 if self.button else BUTTON_BIT)
            uc.mem_write(PI3_ADDR, bytes([cur]))
        elif address == FTM0C_ADDR:
            uc.mem_write(FTM0C_ADDR, (self.ears[0]["pos"] & 0xFFFF).to_bytes(2, "little"))
        elif address == FTM1C_ADDR:
            uc.mem_write(FTM1C_ADDR, (self.ears[1]["pos"] & 0xFFFF).to_bytes(2, "little"))

    def _on_rfid_read(self, uc, address, size, _ud):
        """rfid_read_uid(uid_out[8]) entry: the real body runs to completion on
        the stubbed I2C (it just returns a garbage UID), so rather than skip it
        we let it run and correct its result at the return. Capture the out
        buffer (r0) and arm a one-shot hook at the caller's return address (LR).
        No emu_stop / PC-redirect - those break Unicorn's Thumb + IRQ state."""
        self._rfid_buf = uc.reg_read(UC_ARM_REG_R0)
        if self._rfid_ret_hook is None:
            ret = uc.reg_read(UC_ARM_REG_LR) & ~1
            self._rfid_ret_hook = uc.hook_add(UC_HOOK_CODE, self._on_rfid_ret,
                                              begin=ret, end=ret)

    def _on_rfid_ret(self, uc, address, size, _ud):
        """Back in the caller after rfid_read_uid returned: overwrite its result
        from device state - the injected UID + r0=1 (tag present), or r0=0 (no
        tag) - then remove this transient hook."""
        if self.rfid_uid:
            raw = (bytes.fromhex(self.rfid_uid) + b"\x00" * 8)[:8]
            uc.mem_write(self._rfid_buf, raw)
            uc.reg_write(UC_ARM_REG_R0, 1)
        else:
            uc.reg_write(UC_ARM_REG_R0, 0)
        uc.hook_del(self._rfid_ret_hook)
        self._rfid_ret_hook = None

    def _on_run_motor(self, uc, address, size, _ud):
        """run_motor(n, speed, rotation): capture drive state (rotation 0=FORWARD,
        1=REVERSE). No force-return - the real body runs on against stubbed PWM."""
        n = uc.reg_read(UC_ARM_REG_R0) & 0xFF
        rot = uc.reg_read(UC_ARM_REG_R2) & 0xFF
        if n in (1, 2):
            self.ears[n - 1]["run"] = True
            self.ears[n - 1]["dir"] = "reverse" if rot else "forward"

    def _on_stop_motor(self, uc, address, size, _ud):
        """stop_motor(n): mark ear n stopped (real body runs on)."""
        n = uc.reg_read(UC_ARM_REG_R0) & 0xFF
        if n in (1, 2):
            self.ears[n - 1]["run"] = False

    # -- peripheral control protocol (JSON-Lines, #42) -------------------------
    def _apply_injections(self):
        """Apply any inject events whose gate is now satisfied. Each event fires
        once; gates are at_ms (device-ms, self.elapsed_ms) and/or after (a
        console substring). Ungated events fire on the first slice."""
        for ev in self.inject_events:
            if ev.get("_done"):
                continue
            at_ms = ev.get("at_ms")
            after = ev.get("after")
            if at_ms is not None and self.elapsed_ms < at_ms:
                continue
            if after is not None and after.encode("latin1") not in self.console:
                continue
            self._apply_event(ev)
            ev["_done"] = True

    def _apply_event(self, ev):
        t = ev.get("t")
        if t == "button":
            self.button = bool(ev.get("down"))
        elif t == "rfid":
            self.rfid_uid = ev.get("uid")   # hex string, or None to remove
        elif t == "ear":
            n = ev.get("n")
            if n in (1, 2):
                self.ears[n - 1]["pos"] = int(ev.get("pos", 0)) & 0xFFFF

    def _emit_state_if_changed(self):
        """Emit a state snapshot (JSON line) when a discrete field changes. The
        encoder pos is reported but not part of the change key (it ticks every
        slice while an ear runs - it would spam), so a snapshot lands on a
        button/RFID/ear-drive/LED transition and carries the current pos."""
        key = (tuple(self.led_rgb),
               tuple((e["dir"], e["run"]) for e in self.ears),
               self.button, self.rfid_uid)
        if key == self._last_state_key:
            return
        self._last_state_key = key
        snap = {"t": "state", "ms": self.elapsed_ms,
                "leds": [list(c) for c in self.led_rgb],
                "ears": [{"dir": e["dir"], "run": e["run"], "pos": e["pos"]}
                         for e in self.ears],
                "button": self.button, "rfid": self.rfid_uid}
        line = json.dumps(snap)
        out = self.state_file if self.state_file else sys.stderr
        out.write(line + "\n")
        out.flush()

    def _on_rxrdy(self, uc, address, size, _ud):
        """Stage the LSR DR bit before rxrdy_uart reads it - the non-consuming
        peek the REPL's idle event pump (#195) polls between lines. Same
        code-hook-at-entry pattern as _on_getch, but touches only DR: the RBR
        byte itself is staged (and input consumed) by the getch_uart hook when
        the firmware goes on to actually read. Interactive note: _uart_rx_ready
        blocks prefetching one live byte, so an interactive sim parks here just
        as it used to park inside getch_uart - no event pump runs in-sim, which
        matches the sim's no-timer/no-RFID model (nothing could fire anyway)."""
        cur = int.from_bytes(uc.mem_read(UART0_LSR, 4), "little") | UART_LSR_TXRDY
        if self._uart_rx_ready():
            uc.mem_write(UART0_LSR, (cur | UART_LSR_DR).to_bytes(4, "little"))
        else:
            uc.mem_write(UART0_LSR, (cur & ~UART_LSR_DR & 0xFF).to_bytes(4, "little"))

    def _on_getch(self, uc, address, size, _ud):
        """Stage UART0 RX in memory right before getch_uart reads it. Runs as a
        code hook (fires on every call) rather than a MEM_READ hook because
        Unicorn does not reliably re-fire read hooks for getch_uart's tight
        same-address LSR/RBR polling loop. Consumes one input byte iff one is
        available, matching getch_uart's own DR-gated read (it reads RBR only
        when the LSR DR bit is set)."""
        cur = int.from_bytes(uc.mem_read(UART0_LSR, 4), "little") | UART_LSR_TXRDY
        if self._uart_rx_ready():
            uc.mem_write(UART0_LSR, (cur | UART_LSR_DR).to_bytes(4, "little"))
            uc.mem_write(UART0_THR, bytes([self._uart_rx_take() & 0xFF, 0, 0, 0]))
        else:
            uc.mem_write(UART0_LSR, (cur & ~UART_LSR_DR & 0xFF).to_bytes(4, "little"))

    def _on_main(self, uc, address, size, _ud):
        if not self.reached_main:
            self.reached_main = True
            # stderr when the stream must stay a pure transcript (live REPL or
            # --console-only golden test), stdout otherwise.
            log = sys.stderr if (self.interactive or self.console_only) else sys.stdout
            print(f"  -> reached main() @ 0x{address:08x}", file=log, flush=True)

    # -- run --------------------------------------------------------------------
    def run(self):
        u = self.uc
        # Keep stdout a pure transcript when interactive (live) or --console-only
        # (batch golden test); route diagnostics to stderr in both.
        log = sys.stderr if (self.interactive or self.console_only) else sys.stdout
        print(f"entry = 0x{self.entry:08x}"
              + (f", main = 0x{self.main_addr:08x}" if self.main_addr else "")
              + f", budget = {self.budget} insns", file=log)
        # Ctrl-C: stop the current slice and fall through to the summary, rather
        # than letting Unicorn grind on or dumping a traceback. Docker runs this
        # as PID 1, so install our own handler explicitly.
        def _on_sigint(_sig, _frm):
            self.stopped = True
            try:
                self.uc.emu_stop()
            except Exception:
                pass
        try:
            prev_sigint = signal.signal(signal.SIGINT, _on_sigint)
        except (ValueError, OSError):
            prev_sigint = None   # not the main thread; skip
        # Run in ~1 ms instruction slices; between slices deliver the System
        # Timer IRQ (#102) so the LED fade engine advances. One tick is owed per
        # slice; ticks accrue while interrupts are masked and drain once unmasked.
        pc = self.entry
        executed = 0
        # Wall-clock pacing: one slice stands for ~1 ms of device time, so when a
        # speed multiplier is set we hold each slice to that many real ms. Track a
        # running deadline instead of sleeping a fixed amount, so render/IRQ time
        # is absorbed rather than added; resync if we fall far behind.
        pace_deadline = time.monotonic() if self.speed else None
        try:
            while executed < self.budget and not self.stopped:
                slice_n = min(INSNS_PER_MS, self.budget - executed)
                # Resume in the right ISA: emu_start decodes ARM unless bit0 of
                # the start address is set, so carry the CPSR T-bit across slices
                # (otherwise Thumb code mid-stream gets misdecoded as ARM).
                thumb = u.reg_read(UC_ARM_REG_CPSR) & 0x20
                try:
                    u.emu_start(pc | 1 if thumb else pc, 0xFFFFFFF0, count=slice_n)
                except UcError as e:
                    print(f"  emulation stopped: {e}", file=log, flush=True)
                    break
                executed += slice_n
                pc = u.reg_read(UC_ARM_REG_PC)
                if self.stopped:
                    break
                if self.timer_enabled:
                    self.timer_pending += 1
                    if self._fire_timer_irq():         # vectored into the ISR
                        self.timer_pending -= 1
                        pc = u.reg_read(UC_ARM_REG_PC)  # now 0x18
                # Device-ms clock + synthetic ear encoders + peripheral I/O (#42).
                # One slice stands for ~1 ms, so advance the ms clock, tick each
                # running ear's encoder, apply any due inject events, and emit a
                # state snapshot on change.
                self.elapsed_ms += 1
                for e in self.ears:
                    if e["run"]:
                        step = self.encoder_rate if e["dir"] == "forward" else -self.encoder_rate
                        e["pos"] = (e["pos"] + step) & 0xFFFF
                self._apply_injections()
                if self.emit_state:
                    self._emit_state_if_changed()
                if pace_deadline is not None:
                    pace_deadline += (slice_n / INSNS_PER_MS) / (1000.0 * self.speed)
                    lag = pace_deadline - time.monotonic()
                    if lag > 0:
                        time.sleep(lag)
                    elif lag < -0.25:                  # fell far behind; resync
                        pace_deadline = time.monotonic()
        except UcError as e:
            print(f"  emulation stopped: {e}", file=log, flush=True)
        except KeyboardInterrupt:
            self.stopped = True   # e.g. Ctrl-C landed between slices
        finally:
            if prev_sigint is not None:
                signal.signal(signal.SIGINT, prev_sigint)
        if self.leds_tty and self._leds_drawn:
            sys.stdout.write("\n")
            sys.stdout.flush()
        pc = u.reg_read(UC_ARM_REG_PC)
        print("--- result ---", file=log)
        print(f"  final PC        = 0x{pc:08x}", file=log)
        print(f"  reached main    = {self.reached_main}", file=log)
        print(f"  peripheral wr   = {self.periph_writes}", file=log)
        print(f"  timer IRQs      = {self.timer_ticks}", file=log)
        print(f"  LED frames      = {self.led_frames}", file=log)
        # Interactive already streamed the console live; only dump it in batch mode.
        if self.console and not self.interactive:
            if self.console_only:
                sys.stdout.buffer.write(self.console)   # raw transcript, no repr
                sys.stdout.buffer.flush()
            else:
                print(f"  console output = {self.console.decode('latin1')!r}", file=log)
        return 0 if self.reached_main else 1


def main():
    ap = argparse.ArgumentParser(description="Unicorn simulator for lua-firmware ELFs")
    ap.add_argument("elf", help="path to the ELF to run (e.g. bin/hello.elf)")
    ap.add_argument("-n", "--budget", type=int, default=None,
                    help="max instructions to execute (default 300000, or ~2e9 "
                         "when interactive)")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="log every peripheral (MMIO) write")
    ap.add_argument("-i", "--input", default="",
                    help=r"console input fed to the UART (e.g. 'print(2+2)\n'); "
                         r"backslash escapes like \n and \t are interpreted")
    ap.add_argument("-f", "--input-file",
                    help="read console input from this file verbatim (real "
                         "newlines, no escaping) - overrides --input")
    ap.add_argument("-I", "--interactive", action="store_true",
                    help="live REPL: read UART input from this terminal and echo "
                         "the console to stdout (run the container with -it)")
    ap.add_argument("--console-only", action="store_true",
                    help="batch: write ONLY the raw console transcript to stdout "
                         "(diagnostics to stderr) - for golden-output tests")
    ap.add_argument("-L", "--leds", action="store_true",
                    help="show the five RGB LEDs: a live ANSI strip on a TTY, "
                         "else one line per distinct frame (#102)")
    ap.add_argument("--speed", type=float, default=None,
                    help="pace execution to wall-clock time at this multiplier "
                         "(1.0 = real time, 0.25 = quarter speed for a close "
                         "look at an animation); default: as fast as possible")
    ap.add_argument("--inject-file",
                    help="JSON-Lines peripheral control timeline (#42): one "
                         "{\"t\":\"button|rfid|ear\",...} object per line, each "
                         "optionally gated by at_ms (device-ms) and/or after (a "
                         "console substring). Blank lines and #-comments skipped.")
    ap.add_argument("--emit-state", action="store_true",
                    help="emit a device-state JSON snapshot on each change "
                         "(LEDs/ears/button/RFID) to stderr, or --state-file")
    ap.add_argument("--state-file",
                    help="write --emit-state snapshots here instead of stderr")
    args = ap.parse_args()
    if args.input_file:
        with open(args.input_file, "rb") as fh:
            stdin = fh.read()
    else:
        stdin = args.input.encode("latin1").decode("unicode_escape").encode("latin1")
    # Interactive sessions run open-ended (stop on Ctrl-D/EOF), so default to a
    # large budget; batch runs keep the small default unless overridden.
    budget = args.budget if args.budget is not None else (2_000_000_000 if args.interactive else 300_000)
    inject_events = []
    if args.inject_file:
        with open(args.inject_file) as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                inject_events.append(json.loads(line))
    emit_state = args.emit_state or bool(args.state_file)
    state_file = open(args.state_file, "w") if args.state_file else None
    sys.exit(Sim(args.elf, budget, args.verbose, stdin, args.interactive,
                 args.console_only, args.leds, args.speed,
                 inject_events=inject_events, emit_state=emit_state,
                 state_file=state_file).run())


if __name__ == "__main__":
    main()
