#include "fdv_mod_loader.h"

#include "fdv_tuning.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/print_string.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <algorithm>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace {

using InfoFn = const FdModInfo *(*)();
using InitFn = int32_t (*)(const FdModHost *);
using CountFn = int32_t (*)();
using VehicleFn = const FdModVehicle *(*)(int32_t);
using ShutdownFn = void (*)();

#ifdef _WIN32
using LibraryHandle = HMODULE;

auto open_library(const godot::String &p_path) -> LibraryHandle {
    // Godot strings are UTF-16, so hand Windows the wide API directly instead
    // of round-tripping through the local codepage and mangling non-ASCII paths.
    const godot::Char16String wide = p_path.utf16();
    return LoadLibraryW(reinterpret_cast<LPCWSTR>(wide.get_data()));
}

auto close_library(LibraryHandle p_handle) -> void {
    FreeLibrary(p_handle);
}

auto library_error() -> godot::String {
    return "LoadLibrary failed (error %d)" % static_cast<int64_t>(GetLastError());
}

auto resolve_symbol(LibraryHandle p_handle, const char *p_name) -> void * {
    return reinterpret_cast<void *>(GetProcAddress(p_handle, p_name));
}
#else
using LibraryHandle = void *;

auto open_library(const godot::String &p_path) -> LibraryHandle {
    // RTLD_LOCAL: two mods may both define a helper called `lookup`, and one
    // must not silently bind the other's. Everything a mod needs is passed
    // explicitly through FdModHost.
    return dlopen(p_path.utf8().get_data(), RTLD_NOW | RTLD_LOCAL);
}

auto close_library(LibraryHandle p_handle) -> void {
    dlclose(p_handle);
}

auto library_error() -> godot::String {
    const char *err = dlerror();
    return err != nullptr ? godot::String::utf8(err) : godot::String("dlopen failed");
}

auto resolve_symbol(LibraryHandle p_handle, const char *p_name) -> void * {
    // dlsym leaves a stale error behind for optional symbols; clear it so the
    // next failure is the one that is actually reported.
    dlerror();
    return dlsym(p_handle, p_name);
}
#endif

auto mod_extension() -> const char * {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

auto c_string(const char *p_text) -> godot::String {
    return p_text != nullptr ? godot::String::utf8(p_text) : godot::String();
}

auto failure(const godot::String &p_path, const godot::String &p_error) -> godot::Dictionary {
    godot::Dictionary d;
    d["path"] = p_path;
    d["ok"] = false;
    d["error"] = p_error;
    d["vehicles"] = godot::Array();
    return d;
}

auto vehicle_dictionary(const FdModVehicle &p_vehicle, const godot::String &p_mod_id,
                        int32_t p_index) -> godot::Dictionary {
    godot::Array colour;
    colour.append(p_vehicle.body_colour[0]);
    colour.append(p_vehicle.body_colour[1]);
    colour.append(p_vehicle.body_colour[2]);

    godot::Dictionary d;
    // A mod that forgets an id still gets a stable, unique one rather than an
    // empty key that would collide with every other careless mod.
    d["id"] = p_vehicle.id != nullptr && p_vehicle.id[0] != '\0'
        ? c_string(p_vehicle.id)
        : p_mod_id + godot::String(".vehicle_") + godot::String::num_int64(p_index);
    d["name"] = c_string(p_vehicle.name);
    d["description"] = c_string(p_vehicle.description);
    d["mod"] = p_mod_id;
    d["kind"] = p_vehicle.kind;
    d["preset"] = p_vehicle.preset;
    d["body_scale"] = p_vehicle.body_scale > 0.0f ? p_vehicle.body_scale : 1.0f;
    d["body_colour"] = colour;
    d["tuning"] = fdv::tuning_to_dictionary(p_vehicle.car);
    return d;
}

} // namespace

auto fdv::FdModLoader::_bind_methods() -> void {
    godot::ClassDB::bind_method(godot::D_METHOD("scan", "dir"), &FdModLoader::scan);
    godot::ClassDB::bind_method(godot::D_METHOD("mod_vehicles"), &FdModLoader::mod_vehicles);
    godot::ClassDB::bind_method(godot::D_METHOD("loaded_count"), &FdModLoader::loaded_count);
}

fdv::FdModLoader::~FdModLoader() {
    for (const Library &library : libraries) {
        if (library.handle == nullptr) {
            continue;
        }
        auto shutdown = reinterpret_cast<ShutdownFn>(resolve_symbol(library.handle, "fd_mod_shutdown"));
        if (shutdown != nullptr) {
            shutdown();
        }
        close_library(library.handle);
    }
}

auto fdv::FdModLoader::scan(const godot::String &p_dir) -> godot::Array {
    godot::Array results;

    const godot::Ref<godot::DirAccess> dir = godot::DirAccess::open(p_dir);
    if (dir.is_null()) {
        results.append(failure(p_dir, "cannot open directory"));
        return results;
    }

    godot::PackedStringArray names = dir->get_files();
    names.sort();

    const godot::String suffix = mod_extension();
    for (const godot::String &name : names) {
        if (!name.ends_with(suffix)) {
            continue;
        }

        const godot::String path = p_dir.path_join(name);

        // A rescan is normal (the picker refreshes); loading the same library
        // twice would double its vehicles and run its initialiser twice.
        const bool already_loaded = std::any_of(
            libraries.begin(), libraries.end(),
            [&path](const Library &library) { return library.path == path; });
        if (already_loaded) {
            continue;
        }

        LibraryHandle handle = open_library(path);
        if (handle == nullptr) {
            results.append(failure(path, library_error()));
            continue;
        }

        auto info = reinterpret_cast<InfoFn>(resolve_symbol(handle, "fd_mod_info"));
        if (info == nullptr) {
            close_library(handle);
            results.append(failure(path, "not a vehicle mod (no fd_mod_info)"));
            continue;
        }

        const FdModInfo *mod_info = info();
        if (mod_info == nullptr) {
            close_library(handle);
            results.append(failure(path, "fd_mod_info returned null"));
            continue;
        }

        // Version before anything else: reading `id` out of an unknown struct
        // layout is how a stale mod reads past the end of a smaller struct.
        if (mod_info->abi_version != FD_MOD_API_VERSION) {
            close_library(handle);
            results.append(failure(path,
                godot::String("ABI ") + godot::String::num_int64(mod_info->abi_version) +
                godot::String(", host needs ") + godot::String::num_int64(FD_MOD_API_VERSION)));
            continue;
        }

        auto init = reinterpret_cast<InitFn>(resolve_symbol(handle, "fd_mod_init"));
        auto count = reinterpret_cast<CountFn>(resolve_symbol(handle, "fd_mod_vehicle_count"));
        auto vehicle = reinterpret_cast<VehicleFn>(resolve_symbol(handle, "fd_mod_vehicle"));
        if (init == nullptr || count == nullptr || vehicle == nullptr) {
            close_library(handle);
            results.append(failure(path, "missing fd_mod_init / fd_mod_vehicle_count / fd_mod_vehicle"));
            continue;
        }

        const FdModHost host{ FD_MOD_API_VERSION, &FdModLoader::host_log,
                              &FdModLoader::host_car_default_tuning };
        const int32_t status = init(&host);
        if (status != FD_OK) {
            close_library(handle);
            results.append(failure(path, godot::String("fd_mod_init returned status ") +
                godot::String::num_int64(status)));
            continue;
        }

        const godot::String mod_id = c_string(mod_info->id);

        godot::Dictionary mod;
        mod["path"] = path;
        mod["ok"] = true;
        // Identity for the host's permission list and, later, for a session
        // manifest: two files with the same name are not the same mod.
        mod["sha256"] = godot::FileAccess::get_sha256(path);
        mod["id"] = mod_id;
        mod["name"] = c_string(mod_info->name);
        mod["version"] = c_string(mod_info->version);
        mod["author"] = c_string(mod_info->author);
        mod["description"] = c_string(mod_info->description);

        godot::Array mod_vehicles;
        const int32_t count_value = count();
        for (int32_t i = 0; i < count_value; i++) {
            const FdModVehicle *entry = vehicle(i);
            if (entry == nullptr) {
                continue;
            }
            // The host only knows two rule sets; a mod built against a newer
            // header may declare a third. Skip it rather than step garbage.
            if (entry->kind != FD_MOD_VEHICLE_SCOOTER && entry->kind != FD_MOD_VEHICLE_CAR) {
                host_log(FD_MOD_LOG_WARN, "skipping a vehicle with an unknown kind");
                continue;
            }
            godot::Dictionary converted = vehicle_dictionary(*entry, mod_id, i);
            mod_vehicles.append(converted);
            vehicles.append(converted);
        }
        mod["vehicles"] = mod_vehicles;

        libraries.push_back(Library{ handle, path });
        results.append(mod);
    }

    return results;
}

auto fdv::FdModLoader::mod_vehicles() const -> godot::Array {
    return vehicles;
}

auto fdv::FdModLoader::loaded_count() const -> int64_t {
    return static_cast<int64_t>(libraries.size());
}

auto fdv::FdModLoader::host_log(int32_t p_level, const char *p_message) -> void {
    const godot::String text = godot::String("[mod] ") + c_string(p_message);
    switch (p_level) {
    case FD_MOD_LOG_ERROR:
        godot::_err_print_error(FUNCTION_STR, __FILE__, __LINE__, text, false, false);
        break;
    case FD_MOD_LOG_WARN:
        godot::_err_print_error(FUNCTION_STR, __FILE__, __LINE__, text, false, true);
        break;
    default:
        godot::print_line(text);
        break;
    }
}

auto fdv::FdModLoader::host_car_default_tuning(FdCarTuning *r_out) -> void {
    fd_car_default_tuning(r_out);
}
