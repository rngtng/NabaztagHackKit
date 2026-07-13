# Boot

`boot.mtl` - the first MTL program that runs on the Nabaztag: WiFi provisioning + OTA,
loaded unconditionally at power-on. Model:
[`docs/firmware/architecture.md`](../../docs/firmware/architecture.md).

This is the **pristine** modular split of Violet's monolithic boot — the one
`mtl:firmware:build` embeds by default. [`../bootV2/`](../bootV2/README.md) is
the same boot converged onto the shared `mtl/lib/net` stack (#47/#103).

```
task mtl:boot:build       # compile boot.mtl -> bytecode
task mtl:boot:simulate    # run it in the mtl_linux simulator
```
