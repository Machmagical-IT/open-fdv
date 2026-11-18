#ifndef FDV_TUNING_H
#define FDV_TUNING_H

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <fd_api.h>

namespace fdv {

// FdCarTuning <-> the GDScript dictionary shape used by ManualCar.
//
// Shared so the three places that speak this shape cannot drift apart: the
// hot-refresh entry point (apply_tuning), the readback that an effect snapshots
// before changing something (get_tuning), and the mod loader, which hands a
// mod's declared tuning to the same apply_tuning() the game uses.
inline auto tuning_to_dictionary(const FdCarTuning &p_tuning) -> godot::Dictionary {
    godot::Array ratios;
    for (float ratio : p_tuning.gear_ratios) {
        ratios.append(ratio);
    }

    godot::Dictionary d;
    d["max_torque"] = p_tuning.max_torque;
    d["idle_rpm"] = p_tuning.idle_rpm;
    d["redline_rpm"] = p_tuning.redline_rpm;
    d["gear_ratios"] = ratios;
    d["gear_count"] = p_tuning.gear_count;
    d["final_drive_ratio"] = p_tuning.final_drive_ratio;
    d["wheel_radius"] = p_tuning.wheel_radius;
    d["mass"] = p_tuning.mass;
    d["drag"] = p_tuning.drag;
    d["steer_rate"] = p_tuning.steer_rate;
    return d;
}

// Partial override: every key present replaces that field, absent keys keep the
// current value. A zeroed car is not a useful default, so "only what you name"
// is the contract.
inline auto tuning_override(const FdCarTuning &p_current,
                            const godot::Dictionary &p_overrides) -> FdCarTuning {
    FdCarTuning next = p_current;
    if (p_overrides.has("max_torque")) {
        next.max_torque = static_cast<float>(static_cast<double>(p_overrides["max_torque"]));
    }
    if (p_overrides.has("idle_rpm")) {
        next.idle_rpm = static_cast<float>(static_cast<double>(p_overrides["idle_rpm"]));
    }
    if (p_overrides.has("redline_rpm")) {
        next.redline_rpm = static_cast<float>(static_cast<double>(p_overrides["redline_rpm"]));
    }
    if (p_overrides.has("final_drive_ratio")) {
        next.final_drive_ratio = static_cast<float>(static_cast<double>(p_overrides["final_drive_ratio"]));
    }
    if (p_overrides.has("gear_count")) {
        next.gear_count = static_cast<int32_t>(static_cast<int64_t>(p_overrides["gear_count"]));
    }
    if (p_overrides.has("wheel_radius")) {
        next.wheel_radius = static_cast<float>(static_cast<double>(p_overrides["wheel_radius"]));
    }
    if (p_overrides.has("mass")) {
        next.mass = static_cast<float>(static_cast<double>(p_overrides["mass"]));
    }
    if (p_overrides.has("drag")) {
        next.drag = static_cast<float>(static_cast<double>(p_overrides["drag"]));
    }
    if (p_overrides.has("steer_rate")) {
        next.steer_rate = static_cast<float>(static_cast<double>(p_overrides["steer_rate"]));
    }
    if (p_overrides.has("gear_ratios")) {
        const godot::Array ratios = p_overrides["gear_ratios"];
        const int n = ratios.size();
        for (int i = 0; i < 6; i++) {
            next.gear_ratios[i] = i < n ? static_cast<float>(static_cast<double>(ratios[i])) : 0.0f;
        }
    }
    return next;
}

} // namespace fdv

#endif // FDV_TUNING_H
