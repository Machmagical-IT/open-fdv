# open-fdv

opensource Godot vehicle module that firstly built for Fatal-Delivery

## What it is

The kernel half of the vehicle stack: a Godot 4 GDExtension that binds the
engine-neutral C ABI (`fd_api.h`) and loads native vehicle mods. It knows how to
step a vehicle, resolve a collision, hot-refresh a tuning, and `dlopen` a mod —
it does not know what a vehicle costs, which one the player unlocked, or how the
game's UI should look. Those are the game's job.

## Classes

| Class | Role |
|---|---|
| `VehicleBase` | Arcade scooter (`FdVehicle`), handling presets, collision, replication adopt |
| `ManualCar` | Manual-transmission car (`FdCar`), gears, clutch, faults, `apply_tuning`/`get_tuning` |
| `FdvCore` | Thin Godot wrapper over the rest of the C core (voice, box, orders, monster, mood) |
| `FdModLoader` | Scans native mods, checks `FD_MOD_API_VERSION`, returns vehicle definitions as dictionaries |

## Building

See `BUILDING.md`. Two things are not vendored: the closed rules core
(`Fatal-Delivery/shared`) and the public SDK (`open-fdv-dev`). `SConstruct`
probes both layouts and names the paths it tried if either is missing.

## Mods

A mod is a shared library exporting the entry points in open-fdv-dev's
`fd_mod.h`. It declares vehicles (kind, tuning, handling preset, presentation
hints); the kernel executes them. A broken mod is reported as
`{ok: false, error: ...}` and does not stop the others.

```gdscript
var loader := FdModLoader.new()
for result in loader.scan("/path/to/mods"):
    if result["ok"]:
        for vehicle in result["vehicles"]:
            print(vehicle["name"], " ", vehicle["id"])
    else:
        push_warning("%s: %s" % [result["path"], result["error"]])
```

The loader never passes a mod's own C++ types across the boundary: it copies
each definition into plain dictionaries, so the game can list, filter and save
vehicles without depending on the library staying loaded.

See `open-fdv-dev/docs/mod.md` for the authoring side.
