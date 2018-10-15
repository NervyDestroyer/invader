/*
 * Invader (c) 2018 Kavawuvi
 *
 * This program is free software under the GNU General Public License v3.0 or later. See LICENSE for more information.
 */

#pragma once

#include "../../hek/data_type.hpp"

namespace Invader::HEK {
    struct ResourceMapHeader {
        /**
         * Type of resource map
         */
        LittleEndian<std::uint32_t> type;

        /**
         * Offset to paths
         */
        LittleEndian<std::uint32_t> paths;

        /**
         * Offset to resource indices
         */
        LittleEndian<std::uint32_t> resources;

        /**
         * Number of resources
         */
        LittleEndian<std::uint32_t> resource_count;
    };
    static_assert(sizeof(ResourceMapHeader) == 0x10);

    struct ResourceMapResource {
        /**
         * Resource path offset from tag paths offset
         */
        LittleEndian<std::uint32_t> path_offset;

        /**
         * Size of resource
         */
        LittleEndian<std::uint32_t> size;

        /**
         * Ofset of resource data
         */
        LittleEndian<std::uint32_t> data_offset;
    };
    static_assert(sizeof(ResourceMapResource) == 0xC);
}
