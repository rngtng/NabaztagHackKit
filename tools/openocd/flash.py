#!/usr/bin/env python3
"""Flash a firmwareV2 ELF to a Nabaztag over JTAG (M2, issue #90).

JTAG flashing is the one SDK step that cannot run in Docker - it needs the
physical JTAG adapter - so it runs on your workstation and drives a Raspberry Pi
that is wired to the rabbit and runs the patched OpenOCD 0.8.0
(see tools/openocd/README.md). This script is the host-side orchestrator:

    copy configs + ELF to the Pi -> start OpenOCD (background) ->
    confirm the JTAG chain (IDCODE) -> gdb load + compare-sections + reset run ->
    always tear OpenOCD down.

Setup-specific values are argument DEFAULTS, not hardcoded: point --host / --cfg
/ --remote-dir at another rig and it works. What is *structurally* specific is
the remote-Pi model itself (scp/ssh + sudo for /dev/mem). Generalising to a
local FTDI probe (Bus Blaster + nabaztagv2.cfg, OpenOCD on this host, no
scp/ssh/sudo) is a future `--local` path - the copy/start/stop steps are kept in
their own functions so that seam is a branch, not a rewrite. See the README.
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

# ML67Q4051 JTAG TAP id. Seeing this in the OpenOCD log is the make-or-break
# proof the chain + wiring are good, before any flash write. Chip-specific;
# expose as a flag if this script ever targets another MCU.
IDCODE = "0x3f0f0f0f"
REMOTE_LOG = "/tmp/nab-openocd.log"
GDB_PORT = 3333


def sh(cmd, **kw):
    """Run a local command, echoing it; raise CalledProcessError on nonzero."""
    print(f"  $ {' '.join(cmd)}", flush=True)
    return subprocess.run(cmd, text=True, **kw)


def ssh(host, script, **kw):
    """Run one remote shell command over ssh."""
    return sh(["ssh", host, script], **kw)


def copy_to_pi(a):
    """Ship this repo's OpenOCD configs + the ELF to the Pi (repo = sole source)."""
    print(f"[1/5] copy configs + {a.elf.name} -> {a.host}:{a.remote_dir}/")
    ssh(a.host, f"mkdir -p {a.remote_dir}", check=True)
    # -r . copies the cfgs and the target/ subdir; small, so re-ship every run
    # to keep the Pi in sync with the repo.
    sh(["scp", "-rq", f"{a.configs}/.", f"{a.host}:{a.remote_dir}/"], check=True)
    sh(["scp", "-q", str(a.elf), f"{a.host}:{a.remote_dir}/"], check=True)


def start_openocd(a):
    """Launch OpenOCD on the Pi in the background, logging to REMOTE_LOG."""
    print(f"[2/5] start OpenOCD ({a.cfg}) on {a.host}")
    # sudo: bcm2835gpio needs /dev/mem. nohup + & so the ssh call returns while
    # the gdb server keeps running for step 4.
    ssh(a.host,
        f"sudo rm -f {REMOTE_LOG} && cd {a.remote_dir} && "
        f"sudo bash -c 'nohup {a.openocd} -f {a.cfg} > {REMOTE_LOG} 2>&1 &'",
        check=True)


def wait_for_chain(a):
    """Poll the OpenOCD log until the IDCODE appears, or fail on error/timeout."""
    print(f"[3/5] confirm JTAG chain (IDCODE {IDCODE})")
    deadline = time.monotonic() + a.timeout
    while time.monotonic() < deadline:
        found = ssh(a.host, f"grep -Eq 'tap/device found: {IDCODE}' {REMOTE_LOG}",
                    check=False)
        if found.returncode == 0:
            print(f"      OK - CPU alive, chain good.")
            return
        err = ssh(a.host,
                  f"grep -Eqi 'Error|interrogation failed|all ones|all zeroes' {REMOTE_LOG}",
                  check=False)
        if err.returncode == 0:
            break
        time.sleep(1)
    log = ssh(a.host, f"cat {REMOTE_LOG}", check=False, capture_output=True)
    sys.stderr.write(log.stdout or "")
    raise SystemExit(
        "\nFAIL: JTAG chain not confirmed. Check wiring (esp. JTAG pin 1 / GND) "
        "and that the rabbit is powered from its own PSU. See tools/openocd/README.md.")


def gdb_flash(a):
    """Program flash over the gdb server: load + verify + reset into new firmware."""
    print(f"[4/5] flash {a.elf.name}: load + compare-sections + reset run")
    # gdb runs after `cd {remote_dir}`, so reference the ELF by basename.
    r = ssh(a.host,
            f"cd {a.remote_dir} && {a.gdb} -q -nx {a.elf.name} "
            f"-ex 'set pagination off' -ex 'set confirm off' "
            f"-ex 'target extended-remote localhost:{GDB_PORT}' "
            f"-ex load -ex compare-sections -ex 'monitor reset run' -ex quit",
            check=False, capture_output=True)
    out = (r.stdout or "") + (r.stderr or "")
    print(out, flush=True)
    if "matched" not in out or "Load failed" in out or r.returncode != 0:
        raise SystemExit("\nFAIL: flash load/verify did not succeed (see gdb output above).")
    print("      OK - flash written and verified (compare-sections matched).")


def stop_openocd(a):
    """Always tear the bridge down so the next run (or a manual session) is clean."""
    print("[5/5] stop OpenOCD on the Pi")
    ssh(a.host, "sudo pkill -x openocd || true", check=False)


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--elf", required=True, type=Path, help="path to the ELF to flash")
    p.add_argument("--configs", default="tools/openocd", type=Path,
                   help="dir holding the OpenOCD .cfg files + target/ (default: tools/openocd)")
    p.add_argument("--host", default="tobi@jtag.local",
                   help="ssh target of the Pi JTAG bridge (default: tobi@jtag.local)")
    p.add_argument("--cfg", default="nabaztag-pi.cfg",
                   help="OpenOCD adapter config, relative to the remote dir "
                        "(default: nabaztag-pi.cfg; use nabaztagv2.cfg for a Bus Blaster)")
    p.add_argument("--remote-dir", default="openocd",
                   help="working dir on the Pi, relative to $HOME (default: openocd)")
    p.add_argument("--openocd", default="/usr/local/bin/openocd",
                   help="patched OpenOCD 0.8.0 path on the Pi")
    p.add_argument("--gdb", default="gdb-multiarch", help="gdb binary on the Pi")
    p.add_argument("--timeout", type=int, default=20,
                   help="seconds to wait for the IDCODE before giving up (default: 20)")
    return p.parse_args()


def main():
    a = parse_args()
    if not a.elf.is_file():
        raise SystemExit(f"ELF not found: {a.elf} (build it first: task build:firmwareV2)")
    print(f"Flashing {a.elf} to {a.host} via JTAG\n")
    copy_to_pi(a)
    start_openocd(a)
    try:
        wait_for_chain(a)
        gdb_flash(a)
    finally:
        stop_openocd(a)
    print(f"\nDone. {a.elf.name} is resident and running on the device.")


if __name__ == "__main__":
    main()
