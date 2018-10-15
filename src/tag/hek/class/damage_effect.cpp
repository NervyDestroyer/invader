/*
 * Invader (c) 2018 Kavawuvi
 *
 * This program is free software under the GNU General Public License v3.0 or later. See LICENSE for more information.
 */

#include "../compile.hpp"

#include "damage_effect.hpp"

namespace Invader::HEK {
    void compile_damage_effect_tag(CompiledTag &compiled, const std::byte *data, std::size_t size) {
        BEGIN_COMPILE(DamageEffect);

        ADD_DEPENDENCY_ADJUST_SIZES(tag.sound)

        FINISH_COMPILE;
    }
}
