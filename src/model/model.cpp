// SPDX-License-Identifier: GPL-3.0-only

#include <vector>
#include <cstring>
#include <regex>
#include <list>

#include <invader/version.hpp>
#include <invader/printf.hpp>
#include <invader/file/file.hpp>
#include <invader/command_line_option.hpp>
#include <invader/model/jms.hpp>
#include <invader/tag/parser/parser.hpp>
#include <invader/tag/parser/compile/model.hpp>

enum ModelType {
    MODEL_TYPE_MODEL = 0,
    MODEL_TYPE_GBXMODEL
};

const char *MODEL_EXTENSIONS[] = {
    ".model",
    ".gbxmodel"
};

template <typename T, Invader::HEK::TagFourCC fourcc> std::vector<std::byte> make_model_tag(const std::filesystem::path &path, const std::vector<std::filesystem::path> &tags, const Invader::JMSMap &map) {
    using namespace Invader;
    
    // Load the tag if possible
    std::unique_ptr<Parser::ParserStruct> tag;
    if(std::filesystem::exists(path)) {
        auto old_file = File::open_file(path);
        if(old_file.has_value()) {
            try {
                tag = Parser::ParserStruct::parse_hek_tag_file(old_file->data(), old_file->size());
            }
            catch(std::exception &e) {
                eprintf_error("Failed to parse %s: %s", path.string().c_str(), e.what());
                std::exit(EXIT_FAILURE);
            }
        }
        else {
            eprintf_error("Failed to open %s", path.string().c_str());
            std::exit(EXIT_FAILURE);
        }
    }
    else {
        tag = Parser::ParserStruct::generate_base_struct(fourcc);
    }
    
    // Is it valid?
    auto *model_tag = dynamic_cast<T *>(tag.get());
    if(model_tag == nullptr) {
        eprintf_error("Failed to parse %s (probably not a %s tag)", path.string().c_str(), HEK::tag_fourcc_to_extension(fourcc));
        std::exit(EXIT_FAILURE);
    }
    
    // We don't use local nodes
    model_tag->flags &= ~HEK::ModelFlagsFlag::MODEL_FLAGS_FLAG_PARTS_HAVE_LOCAL_NODES;
    
    // Clear this stuff
    model_tag->markers.clear();
    model_tag->nodes.clear();
    model_tag->regions.clear();
    model_tag->geometries.clear();
    model_tag->shaders.clear();
    model_tag->node_list_checksum = 0;
    
    // LoDs
    const char * const lods[] = {
        "superhigh",
        "high",
        "medium",
        "low",
        "superlow"
    };
    
    // Get our nodes
    std::vector<JMS::Node> nodes;
    
    // Sort JMSes into permutations
    std::map<std::string, JMSMap> permutations;
    std::string top_permutation;
    std::string top_lod;
    
    // Get regions and shaders
    std::vector<std::string> regions;
    
    for(auto &jms : map) {
        auto jms_data_copy = jms.second;
        
        std::string lod = lods[0];
        std::string permutation = jms.first;
        
        // Find the string and LoD
        const auto *str = permutation.c_str();
        for(const auto *i = str; *i != 0; i++) {
            if(*i == ' ') {
                for(const auto *l : lods) {
                    if(std::strcmp(l, i + 1) == 0) {
                        lod = l;
                        permutation = std::string(str, i);
                        goto spaghetti_code_loop_done;
                    }
                }
            }
        }
        
        // Get the permutation map
        spaghetti_code_loop_done:
        auto &permutation_map = permutations[permutation];
        if(permutation_map.find(lod) != permutation_map.end()) {
            eprintf_error("Permutation %s has multiple %s LoDs", permutation.c_str(), lod.c_str());
            std::exit(EXIT_FAILURE);
        }
        
        // Make sure it has nodes!
        if(jms_data_copy.nodes.empty()) {
            eprintf_error("Permutation %s's %s LoD has no nodes", permutation.c_str(), lod.c_str());
            std::exit(EXIT_FAILURE);
        }
        
        // If we haven't added nodes, add them
        if(nodes.empty()) {
            nodes = jms_data_copy.nodes;
            top_permutation = permutation;
            top_lod = lod;
        }
        
        // Otherwise, make sure we have the same nodes
        if(nodes != jms_data_copy.nodes) {
            eprintf_error("Permutation %s's %s LoD does not match permutation %s's %s LoD's node", permutation.c_str(), lod.c_str(), top_permutation.c_str(), top_lod.c_str());
            std::exit(EXIT_FAILURE);
        }
        
        
        // Bounds check!
        auto material_count = jms_data_copy.materials.size();
        auto region_count = jms_data_copy.regions.size();
        for(auto &i : jms_data_copy.triangles) {
            if(i.shader >= material_count) {
                eprintf_error("Permutation %s's %s LoD has an out-of-bounds shader index", permutation.c_str(), lod.c_str());
                std::exit(EXIT_FAILURE);
            }
            if(i.region >= region_count) {
                eprintf_error("Permutation %s's %s LoD has an out-of-bounds region index", permutation.c_str(), lod.c_str());
                std::exit(EXIT_FAILURE);
            }
        }
        
        // Add any regions it may have
        for(std::size_t i = 0; i < region_count; i++) {
            auto &r = jms_data_copy.regions[i];
            std::size_t new_region_index = 0;
            auto iterator = regions.begin();
            bool has_it = false;
            for(; iterator != regions.end(); iterator++, new_region_index++) {
                // The regions already exists
                if(r.name == *iterator) {
                    has_it = true;
                    break;
                }
                
                // This string value is less than it, so it goes before it
                else if(r.name < *iterator) {
                    break;
                }
            }
            
            // Add the new region if it doesn't exist
            if(!has_it) {
                regions.insert(iterator, r.name);
            }
            
            // Fix all the triangles to point to the new region
            for(auto &t : jms_data_copy.triangles) {
                if(t.region == i) {
                    t.region = new_region_index;
                }
            }
        }
        
        // Add any shaders it may have, too
        for(std::size_t mat = 0; mat < material_count; mat++) {
            auto shader_name = jms_data_copy.materials[mat].name;
            if(shader_name.empty()) {
                eprintf_error("Permutation %s's %s LoD has an empty shader name", permutation.c_str(), lod.c_str());
                std::exit(EXIT_FAILURE);
            }
            
            // Find any trailing numbers at the end
            HEK::Index shader_index = 0;
            bool trailing_numbers = false;
            for(std::size_t q = shader_name.size() - 1; q > 0; q--) {
                if(shader_name[q] >= '0' && shader_name[q] <= '9') {
                    trailing_numbers = true;
                }
                else {
                    if(trailing_numbers) {
                        q++; // Add 1 to get the number thing
                        
                        // Check index thing here
                        try {
                            if((shader_index = std::stoul(shader_name.substr(q))) >= NULL_INDEX) {
                                throw std::out_of_range("invalid index");
                            }
                        }
                        catch(std::exception &) {
                            eprintf_error("Permutation %s's %s LoD has an invalid shader name %s", permutation.c_str(), lod.c_str(), shader_name.c_str());
                            std::exit(EXIT_FAILURE);
                        }
                        
                        q--; // subtract 1 to go back before the number
                        while(q > 0 && shader_name[q] == ' ') { // remove trailing spaces
                            q--;
                        }
                        
                        shader_name = shader_name.substr(0, q + 1);
                    }
                    break;
                }
            }
            
            // Did we add it previously?
            bool shader_permutation_exists = false;
            std::size_t new_shader_index = model_tag->shaders.size();
            for(std::size_t i = 0; i < new_shader_index; i++) {
                auto &shader = model_tag->shaders[i];
                if(shader.shader.path == shader_name && shader.permutation == shader_index) {
                    shader_permutation_exists = true;
                    new_shader_index = i;
                    break;
                }
            }
            
            // Nope? Okay. Add it then!
            if(!shader_permutation_exists) {
                auto &shader = model_tag->shaders.emplace_back();
                shader.shader.path = shader_name;
                shader.permutation = shader_index;
            }
            
            // Fix all the triangles to point to the new material
            for(auto &i : jms_data_copy.triangles) {
                if(i.shader == shader_index) {
                    i.shader = new_shader_index;
                }
            }
        }
        
        permutation_map.emplace(lod, jms_data_copy); // Now add it
    }
    
    // Add nodes
    for(auto &n : nodes) {
        auto &node = model_tag->nodes.emplace_back();
        std::strncpy(node.name.string, n.name.c_str(), sizeof(node.name.string) - 1);
        node.next_sibling_node_index = n.sibling_node;
        node.first_child_node_index = n.first_child;
        node.default_translation = n.position;
        node.default_rotation = n.rotation;
        node.parent_node_index = NULL_INDEX;
    }
    
    // Bounds check the indices
    auto node_count = nodes.size();
    for(std::size_t n = 0; n < node_count; n++) {
        auto &node = nodes[n];
        if(node.first_child != NULL_INDEX && node.first_child >= node_count) {
            eprintf_error("Node %s has an out-of-bounds first child index", node.name.c_str());
            std::exit(EXIT_FAILURE);
        }
        if(node.sibling_node != NULL_INDEX && node.sibling_node >= node_count) {
            eprintf_error("Node %s has an out-of-bounds sibling node index", node.name.c_str());
            std::exit(EXIT_FAILURE);
        }
    }
    
    // Make sure we don't have infinite loops with sibling indices, too
    for(std::size_t n = 0; n < node_count; n++) {
        auto &node = nodes[n];
        std::size_t q = 0;
        auto sibling_node = nodes[nodes[n].sibling_node].sibling_node;
        while(sibling_node != NULL_INDEX) {
            sibling_node = nodes[sibling_node].sibling_node;
            if(q++ > node_count) {
                eprintf_error("Infinite loop detected with node %s's sibling index", node.name.c_str());
                std::exit(EXIT_FAILURE);
            }
        }
    }
    
    // Set parent indices now
    for(std::size_t n = 0; n < node_count; n++) {
        auto &node = nodes[n];
        std::size_t next_node_to_set_parent = node.first_child;
        
        // Keep going until we hit null or something we already set
        while(next_node_to_set_parent != NULL_INDEX) {
            auto &child_node = model_tag->nodes[next_node_to_set_parent];
            
            // Did we already set a parent? Bail.
            if(child_node.parent_node_index != NULL_INDEX) {
                break;
            }
            
            // Calculate this
            child_node.node_distance_from_parent = child_node.default_translation.distance_from_point(HEK::Point3D<HEK::NativeEndian> {});
            child_node.parent_node_index = n;
            
            // Next node?
            next_node_to_set_parent = nodes[n].sibling_node;
        }
    }
    
    // Add regions to the model tag
    for(auto &i : regions) {
        auto &region = model_tag->regions.emplace_back();
        std::strncpy(region.name.string, i.c_str(), sizeof(region.name.string) - 1);
    }
    
    // Go through each permutation now
    for(auto &i : permutations) {
        for(auto &lod : i.second) {
            auto &jms = lod.second;
            
            // Find all regions this encompasses
            std::vector<std::size_t> regions_we_are_in;
            for(auto &t : jms.triangles) {
                bool in_it = false;
                for(auto &r : regions_we_are_in) {
                    if(r == t.region) {
                        in_it = true;
                    }
                }
                if(!in_it) {
                    regions_we_are_in.emplace_back(t.region);
                }
            }
            
            // Go through each region now...
            for(auto &r : regions_we_are_in) {
                auto &model_tag_region = model_tag->regions[r];
                
                // Is there already an entry in the model tag region permutation array for this?
                bool in_it = false;
                std::size_t permutation_index = model_tag_region.permutations.size();
                for(std::size_t p = 0; p < permutation_index; p++) {
                    if(i.first == model_tag_region.permutations[p].name.string) {
                        in_it = true;
                        permutation_index = p;
                        break;
                    }
                }
                if(!in_it) {
                    auto &p = model_tag_region.permutations.emplace_back();
                    p.super_low = NULL_INDEX;
                    p.low = NULL_INDEX;
                    p.medium = NULL_INDEX;
                    p.high = NULL_INDEX;
                    p.super_high = NULL_INDEX;
                    std::strncpy(p.name.string, i.first.c_str(), sizeof(p.name.string) - 1);
                }
                auto &p = model_tag_region.permutations[permutation_index];
                
                // Are we the superhigh LoD? If so, add markers.
                if(lod.first == "superhigh") {
                    for(auto &ji : jms.markers) {
                        if(ji.region == r) {
                            auto &m = p.markers.emplace_back();
                            std::strncpy(m.name.string, ji.name.c_str(), sizeof(m.name.string) - 1);
                            m.node_index = ji.node;
                            m.rotation = ji.rotation;
                            m.translation = ji.position;
                        }
                    }
                }
                
                // Add our new geometry
                auto new_geometry_index = model_tag->geometries.size();
                auto &geometry = model_tag->geometries.emplace_back();
                
                // Set the index
                if(lod.first == "superhigh") {
                    p.super_high = new_geometry_index;
                }
                else if(lod.first == "high") {
                    p.high = new_geometry_index;
                }
                else if(lod.first == "medium") {
                    p.medium = new_geometry_index;
                }
                else if(lod.first == "low") {
                    p.low = new_geometry_index;
                }
                else if(lod.first == "superlow") {
                    p.super_low = new_geometry_index;
                }
                else {
                    eprintf_error("Eep!");
                    std::exit(EXIT_FAILURE);
                }
                
                // Now for the shader indices
                std::vector<std::size_t> shaders_we_use;
                for(auto &t : jms.triangles) {
                    if(t.region == r) {
                        bool shader_in_it = false;
                        for(auto &s : shaders_we_use) {
                            if(s == t.shader) {
                                shader_in_it = true;
                                break;
                            }
                        }
                        if(!shader_in_it) {
                            shaders_we_use.emplace_back(t.region);
                        }
                    }
                }
                
                // Go through each shader. Add a part thing
                for(auto &s : shaders_we_use) {
                    auto &part = geometry.parts.emplace_back();
                    part.prev_filthy_part_index = ~0;
                    part.next_filthy_part_index = ~0;
                    part.shader_index = s;
                    
                    // Isolate all triangles
                    std::vector<JMS::Triangle> all_triangles_here;
                    for(auto &t : jms.triangles) {
                        if(t.region == r && t.shader == s) {
                            all_triangles_here.emplace_back(t);
                        }
                    }
                    
                    // Isolate all vertices
                    std::map<std::size_t, std::size_t> all_vertices_here_indexed;
                    std::vector<JMS::Vertex> all_vertices_here;
                    for(auto &t : all_triangles_here) {
                        for(auto &v : t.vertices) {
                            // Add the vertex. Note the index of it
                            if(all_vertices_here_indexed.find(v) == all_vertices_here_indexed.end()) {
                                all_vertices_here_indexed[v] = all_vertices_here.size();
                                all_vertices_here.emplace_back(jms.vertices[v]);
                            }
                        }
                    }
                    
                    // Set the vertices now
                    for(auto &t : all_triangles_here) {
                        for(auto &v : t.vertices) {
                            v = all_vertices_here_indexed[v];
                        }
                    }
                    
                    // Add all vertices
                    for(auto &v : all_vertices_here) {
                        auto &vm = part.uncompressed_vertices.emplace_back();
                        vm.position = v.position;
                        vm.normal = v.normal;
                        vm.texture_coords = v.texture_coordinates;
                        vm.node0_index = v.node0;
                        vm.node0_weight = 1.0F - v.node1_weight;
                        vm.node1_index = v.node1;
                        vm.node1_weight = v.node1_weight;
                    }
                    
                    // Calculate binormal/tangent (most of this is from the MEK @ https://github.com/Sigmmma/reclaimer/blob/e9900716d1962f4a172f517791c2f6b7900898c5/reclaimer/model/jms.py - thanks MosesofEgypt!)
                    for(auto &t : all_triangles_here) {
                        static constexpr const std::size_t range = sizeof(t.vertices) / sizeof(*t.vertices);
                        static_assert(range == 3);
                        
                        for(std::size_t v = 0; v < range; v++) {
                            // Get our vertices, binormal, and tangent
                            auto &vertex0 = part.uncompressed_vertices[t.vertices[(v + 0) % range]];
                            auto &vertex1 = part.uncompressed_vertices[t.vertices[(v + 1) % range]];
                            auto &vertex2 = part.uncompressed_vertices[t.vertices[(v + 2) % range]];
                            
                            auto &b = vertex0.binormal;
                            auto &t = vertex0.tangent;
                            
                            // Subtract the x/y/z from the other two vertices
                            float x1 = vertex1.position.x - vertex0.position.x;
                            float x2 = vertex2.position.x - vertex0.position.x;
                            float y1 = vertex1.position.y - vertex0.position.y;
                            float y2 = vertex2.position.y - vertex0.position.y;
                            float z1 = vertex1.position.z - vertex0.position.z;
                            float z2 = vertex2.position.z - vertex0.position.z;
                            
                            // Do the same thing with the texture coordinates
                            float u1 = vertex1.texture_coords.x - vertex0.texture_coords.x;
                            float u2 = vertex2.texture_coords.x - vertex0.texture_coords.x;
                            
                            // Since it's flipped, subtract v from 1 to flip it back.
                            float v1 = (1.0F - vertex1.texture_coords.y) - (1.0F - vertex0.texture_coords.y);
                            float v2 = (1.0F - vertex2.texture_coords.y) - (1.0F - vertex0.texture_coords.y);
                            
                            float r = u1 * v2 - u2 * v1;
                            if(r == 0) {
                                continue;
                            }
                            
                            r = 1.0 / r;
                            
                            // Binormal
                            float bi = -(u1 * x2 - u2 * x1) * r;
                            float bj = -(u1 * y2 - u2 * y1) * r;
                            float bk = -(u1 * z2 - u2 * z1) * r;
                            float b_len = std::sqrt(bi*bi + bj*bj + bk*bk);
                            
                            // Tangent
                            float ti = (v2 * x1 - v1 * x2) * r;
                            float tj = (v2 * y1 - v1 * y2) * r;
                            float tk = (v2 * z1 - v1 * z2) * r;
                            float t_len = std::sqrt(ti*ti + tj*tj + tk*tk);
                            
                            if(b_len > 0) {
                                b.i = b.i + bi / b_len;
                                b.j = b.j + bj / b_len;
                                b.k = b.k + bk / b_len;
                            }
                            
                            if(t_len > 0) {
                                t.i = t.i + ti / t_len;
                                t.j = t.j + tj / t_len;
                                t.k = t.k + tk / t_len;
                            }
                        }
                    }
                    
                    // Normalize vectors
                    for(auto &v : part.uncompressed_vertices) {
                        v.binormal = v.binormal.normalize();
                        v.tangent = v.tangent.normalize();
                    }
                    
                    // Now let's... do this horrible monstrosity, triangle strips!
                    //
                    // Basically, triangles in Halo are stored like this:
                    //
                    // A B C D          A          B          C          D
                    // 0 1 2 3 4 5 6 = (0, 1, 2); (1, 3, 2); (2, 3, 4); (3, 5, 4); (4, 5, 6)
                    //
                    // It can save lots of space, but only if everything is nicely sequenced like this.
                    // If not, you can lose space by having to add degenerate triangles.
                    // On average, it saves a decent amount of space... as far as 16-bit integers go at least.
                    
                    // Add the first triangle
                    std::vector<HEK::Index> triangle_man = { all_triangles_here[0].vertices[0], all_triangles_here[0].vertices[1], all_triangles_here[0].vertices[2] };
                    
                    // Our remaining triangles
                    std::list<JMS::Triangle> remaining_triangles = std::list<JMS::Triangle>(all_triangles_here.begin() + 1, all_triangles_here.end());
                    
                    // Add the rest
                    while(remaining_triangles.size() > 0) {
                        bool normals_flipped = (triangle_man.size() % 2) == 1;
                        
                        HEK::Index current_triangle[3] = {};
                        auto a_index = 0;
                        
                        auto b_index = normals_flipped ? 2 : 1;
                        auto c_index = normals_flipped ? 1 : 2;
                        
                        auto a_index_next = a_index;
                        auto b_index_next = c_index;
                        auto c_index_next = b_index;
                        
                        auto &a = current_triangle[a_index];
                        auto &b = current_triangle[b_index];
                        
                        a = triangle_man[triangle_man.size() - 2];
                        b = triangle_man[triangle_man.size() - 1];
                        
                        // Let's try to find a triangle that can simply go next with only one index
                        bool triangle_found = false;
                        for(auto rt = remaining_triangles.begin(); rt != remaining_triangles.end(); rt++) {
                            // ABC ; BDC -> A B C D
                            if(rt->vertices[a_index] == a && rt->vertices[b_index] == b) {
                                triangle_found = true;
                                triangle_man.emplace_back(rt->vertices[c_index]);
                                remaining_triangles.erase(rt);
                                break;
                            }
                        }
                        if(triangle_found) {
                            continue;
                        }
                        
                        // Try a triangle that can go next but requires two indices
                        for(auto rt = remaining_triangles.begin(); rt != remaining_triangles.end(); rt++) {
                            // ABC ; CBD -> A B C B D
                            if(rt->vertices[a_index_next] == b && rt->vertices[b_index_next] == a) {
                                triangle_man.emplace_back(rt->vertices[b_index_next]);
                                triangle_man.emplace_back(rt->vertices[c_index_next]);
                                triangle_found = true;
                                remaining_triangles.erase(rt);
                                break;
                            }
                        }
                        if(triangle_found) {
                            continue;
                        }
                        
                        // Try a triangle that can go next but requires three indices
                        for(auto rt = remaining_triangles.begin(); rt != remaining_triangles.end(); rt++) {
                            // ABC ; CDE -> A B C C E D
                            if(rt->vertices[a_index] == b) {
                                triangle_man.emplace_back(b);
                                triangle_man.emplace_back(rt->vertices[b_index]);
                                triangle_man.emplace_back(rt->vertices[c_index]);
                                triangle_found = true;
                                remaining_triangles.erase(rt);
                                break;
                            }
                        }
                        if(triangle_found) {
                            continue;
                        }
                        
                        // Last resort - Guarantees we can get the triangle in place but requires five indices
                        // ABC; DEF -> A B C C D D F E
                        triangle_man.emplace_back(b);
                        auto first_triangle = *remaining_triangles.begin();
                        remaining_triangles.erase(remaining_triangles.begin());
                        triangle_man.emplace_back(first_triangle.vertices[a_index]);
                        triangle_man.emplace_back(first_triangle.vertices[a_index]);
                        triangle_man.emplace_back(first_triangle.vertices[b_index]);
                        triangle_man.emplace_back(first_triangle.vertices[c_index]);
                    }
                    
                    // Add null's
                    while(triangle_man.size() % 3 > 0) {
                        triangle_man.emplace_back(NULL_INDEX);
                    }
                    
                    // Add the triangles
                    part.triangles.resize(triangle_man.size() / 3);
                    std::size_t q = 0;
                    for(auto &t : part.triangles) {
                        t.vertex0_index = triangle_man[q++];
                        t.vertex1_index = triangle_man[q++];
                        t.vertex2_index = triangle_man[q++];
                    }
                }
            }
        }
    }
    
    // Get everything
    auto all_tags = File::load_virtual_tag_folder(tags);
    auto prefer_shaders = path.parent_path() / "shaders";
    
    // Resolve shaders
    for(auto &s : model_tag->shaders) {
        std::optional<File::TagFilePath> first_guess;
        
        for(auto &t : all_tags) {
            // Shader tag?
            if(!IS_SHADER_TAG(t.tag_fourcc)) {
                continue;
            }
            
            // Same name?
            if(t.full_path.filename().replace_extension() != s.shader.path) {
                continue;
            }
            
            // Do we have it?
            if(t.full_path.parent_path() == prefer_shaders) {
                first_guess = File::TagFilePath(t.tag_path, t.tag_fourcc);
                break;
            }
            
            // No? Well hold it for now
            else if(!first_guess.has_value()) {
                first_guess = File::TagFilePath(t.tag_path, t.tag_fourcc);
            }
        }
        
        // Did we find it?
        if(first_guess.has_value()) {
            s.shader.path = File::split_tag_class_extension(File::preferred_path_to_halo_path(first_guess->path)).value().path;
            s.shader.tag_fourcc = first_guess->fourcc;
        }
        else {
            eprintf_error("Failed to find a shader tag with the filename %s", s.shader.path.c_str());
            std::exit(EXIT_FAILURE);
        }
    }
    
    // Fix geometries
    for(auto &r : model_tag->regions) {
        for(auto &p : r.permutations) {
            #define REPLACE_IF_NEEDED(from, to) if(to == NULL_INDEX) { to = from; }
            REPLACE_IF_NEEDED(p.super_high, p.high);
            REPLACE_IF_NEEDED(p.high, p.medium);
            REPLACE_IF_NEEDED(p.medium, p.low);
            REPLACE_IF_NEEDED(p.low, p.super_low);
        }
    }
    
    // Generate compressed vertices
    regenerate_missing_model_vertices(*model_tag, true);
    
    return tag->generate_hek_tag_data(fourcc);
}

int main(int argc, const char **argv) {
    using namespace Invader;
    using namespace Invader::HEK;
    
    struct ModelOptions {
        std::optional<ModelType> type;
        std::vector<std::filesystem::path> tags;
        std::filesystem::path data = "data";
        bool filesystem_path = false;
        bool legacy = false;
    } model_options;

    std::vector<Invader::CommandLineOption> options;
    options.emplace_back("info", 'i', 0, "Show credits, source info, and other info.");
    options.emplace_back("legacy", 'L', 0, "Use legacy behavior (use parent folder's filename for the tag name).");
    options.emplace_back("fs-path", 'P', 0, "Use a filesystem path for the tag path or data directory.");
    options.emplace_back("type", 'T', 1, "Specify the type of model. Can be: model, gbxmodel", "<type>");
    options.emplace_back("data", 'd', 1, "Use the specified data directory.", "<dir>");
    options.emplace_back("tags", 't', 1, "Use the specified tags directory. Additional tags directories can be specified for searching shaders, but the tag will be output to the first one.", "<dir>");

    static constexpr char DESCRIPTION[] = "Create a model tag.";
    static constexpr char USAGE[] = "[options] <model-tag>";

    auto remaining_arguments = Invader::CommandLineOption::parse_arguments<ModelOptions &>(argc, argv, options, USAGE, DESCRIPTION, 1, 1, model_options, [](char opt, const auto &args, ModelOptions &model_options) {
        switch(opt) {
            case 'i':
                Invader::show_version_info();
                std::exit(EXIT_SUCCESS);
            case 'P':
                model_options.filesystem_path = true;
                break;
            case 'L':
                model_options.legacy = true;
                break;
            case 'T':
                if(std::strcmp(args[0], "model") == 0) {
                    model_options.type = ModelType::MODEL_TYPE_MODEL;
                }
                else if(std::strcmp(args[0], "gbxmodel") == 0) {
                    model_options.type = ModelType::MODEL_TYPE_GBXMODEL;
                }
                else {
                    eprintf_error("Invalid type %s", args[0]);
                    std::exit(EXIT_FAILURE);
                }
                break;
        }
    });
    
    if(model_options.tags.size() == 0) {
        model_options.tags.emplace_back("tags");
    }
    
    // Do this
    if(!model_options.type.has_value()) {
        eprintf_error("No type specified. Use -h for more information.");
        return EXIT_FAILURE;
    }
    
    const char *extension = MODEL_EXTENSIONS[*model_options.type];
    
    // Handle -P
    std::string model_tag;
    if(model_options.filesystem_path) {
        auto model_tag_maybe = File::file_path_to_tag_path(remaining_arguments[0], model_options.tags);
        if(model_tag_maybe.has_value() && std::filesystem::exists(remaining_arguments[0])) {
            auto path = std::filesystem::path(*model_tag_maybe);
            if(path.extension() == extension) {
                // Legacy - bump up a directory
                if(model_options.legacy) {
                    model_tag = path.parent_path();
                }
                
                // Otherwise use this
                else {
                    model_tag = path.replace_extension().string();
                }
            }
            else {
                eprintf_error("Extension must be %s", remaining_arguments[0]);
                return EXIT_FAILURE;
            }
        }
        else {
            auto model_folder_maybe = File::file_path_to_tag_path(remaining_arguments[0], model_options.data);
            if(model_folder_maybe.has_value() && std::filesystem::exists(remaining_arguments[0])) {
                model_tag = *model_folder_maybe;
            }
            else {
                eprintf_error("Failed to find a valid model %s in the data or tags directories.", remaining_arguments[0]);
                return EXIT_FAILURE;
            }
        }
    }
    else {
        model_tag = remaining_arguments[0];
    }
    
    // Double the filename if legacy
    auto data_dir = model_tag;
    if(model_options.legacy) {
        auto tp = std::filesystem::path(model_tag);
        model_tag = (tp / tp.filename()).string();
    }
    
    // Let's do this
    JMSMap jms_files;
    std::filesystem::path directory = model_options.data / data_dir / "models";
    
    // Does it exist?
    if(!std::filesystem::is_directory(directory)) {
        eprintf_error("No directory exists at %s", directory.string().c_str());
        return EXIT_FAILURE;
    }
    
    // Let's do this
    try {
        // Get paths. Sort alphabetically.
        for(auto &i : std::filesystem::directory_iterator(directory)) {
            auto path = i.path();
            if(path.extension() == ".jms" && i.is_regular_file()) {
                try {
                    auto file = File::open_file(path);
                    if(!file.has_value()) {
                        eprintf_error("Failed to read %s", path.string().c_str());
                        return EXIT_FAILURE;
                    }
                    jms_files.emplace(path.filename().replace_extension(), JMS::from_string(std::string(reinterpret_cast<const char *>(file->data()), file->size()).c_str()));
                }
                catch(std::exception &) {
                    eprintf_error("Failed to parse %s", path.string().c_str());
                    return EXIT_FAILURE;
                }
            }
        }
    }
    catch(std::exception &e) {
        eprintf_error("Failed to iterate through %s: %s", directory.string().c_str(), e.what());
        return EXIT_FAILURE;
    }
    
    // Nothing found?
    if(jms_files.empty()) {
        eprintf_error("No .jms files found in %s", directory.c_str());
        return EXIT_FAILURE;
    }
    
    // Generate a tag
    std::vector<std::byte> tag_data;
    std::filesystem::path file_path = (model_options.tags[0] / (model_tag + extension));
    
    switch(*model_options.type) {
        case ModelType::MODEL_TYPE_MODEL:
            tag_data = make_model_tag<Parser::Model, TagFourCC::TAG_FOURCC_MODEL>(file_path, model_options.tags, jms_files);
            break;
        case ModelType::MODEL_TYPE_GBXMODEL:
            tag_data = make_model_tag<Parser::GBXModel, TagFourCC::TAG_FOURCC_GBXMODEL>(file_path, model_options.tags, jms_files);
            break;
        default:
            std::terminate();
    }
    
    if(!File::save_file(file_path, tag_data)) {
        eprintf_error("Failed to write to %s", file_path.string().c_str());
        return EXIT_FAILURE;
    }
}
