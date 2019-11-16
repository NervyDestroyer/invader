// SPDX-License-Identifier: GPL-3.0-only

#include <cmath>

#include <invader/tag/hek/compile.hpp>
#include <invader/tag/hek/class/lens_flare.hpp>

namespace Invader::HEK {
    void compile_lens_flare_tag(CompiledTag &compiled, const std::byte *data, std::size_t size) {
        BEGIN_COMPILE(LensFlare);
        ADD_DEPENDENCY_ADJUST_SIZES(tag.bitmap);
        DEFAULT_VALUE(tag.vertical_scale, 1.0f);
        DEFAULT_VALUE(tag.horizontal_scale, 1.0f);
        DEFAULT_VALUE(tag.rotation_function_scale, 360.0f);
        ADD_REFLEXIVE_START(tag.reflections) {
            DEFAULT_VALUE(reflexive.animation_period, 1.0f);
        } ADD_REFLEXIVE_END
        tag.cos_falloff_angle = std::cos(tag.falloff_angle);
        tag.cos_cutoff_angle = std::cos(tag.cutoff_angle);
        FINISH_COMPILE
    }
}
