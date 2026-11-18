#ifndef FDV_MOD_LOADER_H
#define FDV_MOD_LOADER_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <vector>

#include <fd_mod.h>

namespace fdv {

// The kernel side of the mod contract: find native mods, check their ABI,
// initialise them, and hand their vehicle definitions to the game.
//
// This class deliberately stops at the ABI boundary. It returns plain
// dictionaries -- no mod-provided C++ types, no callbacks into the game -- so
// the business layer can list, filter and persist vehicles without knowing that
// a shared library is involved. Loading is the kernel's job; deciding what a
// vehicle costs, looks like, or unlocks is the game's.
//
// A mod is trusted code once loaded. There is no sandbox here: the ABI is a
// stability contract, not a security boundary.
class FdModLoader : public godot::RefCounted {
    GDCLASS(FdModLoader, godot::RefCounted)

    // One dlopen'd library. The handle is kept for the lifetime of the loader
    // because a mod's strings and vehicle definitions must stay valid while the
    // game holds copies of them, and because fd_mod_shutdown() is called from
    // the destructor.
    struct Library {
        void *handle = nullptr;
        godot::String path;
    };

    std::vector<Library> libraries;
    godot::Array vehicles;

protected:
    static void _bind_methods();

public:
    FdModLoader() = default;
    ~FdModLoader() override;

    // Scan a directory for native mods and return one dictionary per file, in
    // sorted filename order so the picker is stable between runs:
    //
    //   { path, ok, id, name, version, author, description, vehicles }
    //
    // Failures are reported in the same shape with ok = false and an `error`
    // string; a broken mod must not stop the others from loading, and a silent
    // skip would make the game look like it ignored the file.
    //
    // The path is a filesystem path, not res://: native libraries inside a .pck
    // cannot be dlopen'd, so callers globalize their path first.
    auto scan(const godot::String &p_dir) -> godot::Array;

    // Every vehicle declared by every mod loaded so far, flattened and tagged
    // with the mod that supplied it. The game merges this with its own
    // built-ins; the order tracks load order.
    [[nodiscard]] auto mod_vehicles() const -> godot::Array;

    [[nodiscard]] auto loaded_count() const -> int64_t;

    // Host services passed to fd_mod_init(). Static because the C ABI takes
    // plain function pointers, and they must not depend on a particular loader
    // instance staying alive mid-call.
    static auto host_log(int32_t p_level, const char *p_message) -> void;
    static auto host_car_default_tuning(FdCarTuning *r_out) -> void;
};

} // namespace fdv

#endif // FDV_MOD_LOADER_H
