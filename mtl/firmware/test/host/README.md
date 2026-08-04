# Host-side tests for the mtl firmware's vendored 802.11 stack

`task mtl:firmware:test:host` — also chained into `mtl:firmware:test`, and so
into `mtl:verify`.

These link the **real** `src/net/{ieee80211,eapol}.c` with the host `gcc`
(not the ARM cross-compiler) against a stubbed board layer, and run natively
under AddressSanitizer + UBSan. A memory-safety regression aborts with a
precise report naming the vendored line.

This is the mtl track's **first automated coverage of the WPA join path**
(#230, #307). Before it, `mtl:verify` was builds plus the AES-128/RFC-3394
known-answer test — nothing executed the 802.11 receive path at all.

## Why the vendored net/ sources can be tested at all

They are the part of the firmware that parses attacker-controlled input, and
they turned out to need almost nothing from the board:

```
$ gcc -c -std=gnu11 -D_NAB_SIM -w -Istubs -I../../inc -I../../sys/inc \
      -o /tmp/x.o ../../src/net/ieee80211.c && nm -u /tmp/x.o | wc -l
20
```

Twenty undefined symbols, every one a driver or board entry point that a test
file can define. `eapol.c` needs 23. That is what turns a code-reading
suspicion into an ASan-proven defect.

`stubs/ml60842.h` is the only stub header needed: `hcd.h`'s
`disable_ohci_irq()` dereferences the USB block at its absolute address, which
segfaults off-target. Neither `net/` source touches an SoC register, so
`sys/inc/ml674061.h` is used as-is — unlike the lua track, which also shadows
it for its register-only HAL drivers.

## Running

```sh
task mtl:firmware:test:host      # both binaries, all scenarios, in Docker
make check                       # same, on a host with gcc + ASan
./eapol_test msg3-mic            # one scenario - an abort can't mask the rest
```

An unknown scenario name **exits 2**; it does not report a pass having run
nothing.

## What each scenario pins

`ieee80211_test`

| Scenario | Pins |
|---|---|
| `short` | a legal 32-byte SSID builds a probe request (non-vacuous baseline) |
| `long` | an over-long SSID must not run off the COMRAM probe allocation |
| `rx-legal` | AP mode answers a well-formed probe request **and frees the frame** (#295) |
| `rx-ssid` | AP mode: the SSID IE length byte comes off the air (#293) |
| `rx-walk` | the IE walk must not read a length byte past the frame end (#293) |
| `rx-rsn` | STA scan: the RSN IE's suite counts come off the air (#294) |
| `rx-rsn-adv` | an accepted RSN IE leaves the walk on the next IE, not two bytes into it (#294) |
| `rx-wpa1` | STA scan: the WPA1 vendor IE's suite counts, likewise — **mtl only**, the lua track dropped that parser in #124 |

`eapol_test`

| Scenario | Pins |
|---|---|
| `msg1` | a well-formed message 1/4 is answered (non-vacuous baseline) |
| `msg3-mic` | msg3's `body_length` must be bounded by the received length (#292) |
| `msg3-gtk` | msg3's key data is walked past the leading RSN IE and its GTK KDE installed (#230) |
| `gtk-kd` | the group message's `key_data_length`, likewise bounded (#292) |
| `group-tkip` | the WPA1/TKIP branch's RC4 GTK read, likewise — **mtl only**, #124 removed that branch from the lua twin (#292) |
| `group-mic` | the group-key message's `body_length`, likewise (#292) |

## Reading a failure

`msg3-mic`, `gtk-kd`, `group-*`, `long` and the `rx-*` guards assert that the
run **completes**. When the bound is missing, ASan aborts inside the stubbed
`hmac_sha1` / `aes128_unwrap` / `memcmp`, which is not the stub misbehaving —
an HMAC over N bytes reads N bytes, and N came from the code under test.

The baselines (`msg1`, `short`, `rx-legal`, `rx-rsn-adv`, `msg3-gtk`) assert
positive content, so a guard cannot pass by the parser bailing out before it
reaches the read.

A guard also has to fail against the **original** source, not just against the
previous commit. `gtk-kd` declares 56 bytes of key data rather than the current
112-byte cap for exactly that reason: the scratch buffer was 48+8 bytes before
#230 enlarged it, so a larger value would be refused by the old code for the
wrong reason and the guard would pass having proved nothing.

## Scope

Everything here is the 802.11 receive path with the radio, the USB host
controller and the crypto stubbed. It says nothing about behaviour against a
real AP; that needs the JTAG rig. `msg3-gtk` in particular proves the KDE walk
and the install call, **not** that a WPA2 join keys broadcasts correctly on
hardware — see #230.
