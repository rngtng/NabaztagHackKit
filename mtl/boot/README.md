# Boot

`boot.mtl` - the first MTL program that runs on the Nabaztag: WiFi provisioning + OTA,
loaded unconditionally at power-on. Model:
[`docs/firmware/architecture.md`](../../docs/firmware/architecture.md).

```
task mtl:boot:build       # compile boot.mtl -> bytecode
task mtl:boot:simulate    # run it in the mtl_linux simulator
```
