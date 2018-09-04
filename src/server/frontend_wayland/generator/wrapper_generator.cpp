/*
 * Copyright © 2017 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored By: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#include "emitter.h"
#include "interface.h"
#include "utils.h"

#include <libxml++/libxml++.h>
#include <iostream>

Emitter comment_header(std::string const& input_file_path)
{
    return Lines{
        "/*",
        " * AUTOGENERATED - DO NOT EDIT",
        " *",
        {" * This file is generated from ", file_name_from_path(input_file_path)},
        " * To regenerate, run the “refresh-wayland-wrapper” target.",
        " */",
    };
}

Emitter include_guard_top(std::string const& macro)
{
    return Lines{
        {"#ifndef ", macro},
        {"#define ", macro},
    };
}

Emitter header_includes()
{
    return Lines{
        "#include <experimental/optional>",
        empty_line,
        "#include \"mir/fd.h\"",
        "#include \"../wayland_utils.h\"",
    };
}

Emitter impl_includes(std::string const& protocol_name)
{
    return Lines{
        {"#include \"", protocol_name, "_wrapper.h\""},
        {"#include \"", protocol_name, ".h\""},
        empty_line,
        "#include <boost/throw_exception.hpp>",
        "#include <boost/exception/diagnostic_information.hpp>",
        empty_line,
        "#include <wayland-server-core.h>",
        empty_line,
        "#include \"mir/log.h\"",
    };
}

Emitter include_guard_bottom(std::string const& macro)
{
    return Lines{
        {"#endif // ", macro}
    };
}

Emitter header_file(std::string input_file_path, std::vector<Interface> const& interfaces)
{
    std::string const include_guard_macro = to_upper_case("MIR_FRONTEND_WAYLAND_" + file_name_from_path(input_file_path) + "_WRAPPER");

    std::vector<Emitter> interface_emitters;
    for (auto const& interface : interfaces)
    {
        interface_emitters.push_back(interface.declaration());
    }

    std::vector<Emitter> fwd_declare_emitters;
    for (auto const& interface : interfaces)
    {
        fwd_declare_emitters.push_back(interface.global_namespace_forward_declarations());
    }

    return Lines{
        comment_header(input_file_path),
        empty_line,
        include_guard_top(include_guard_macro),
        empty_line,
        header_includes(),
        empty_line,
        List{fwd_declare_emitters, nullptr},
        empty_line,
        "namespace mir",
        "{",
        "namespace frontend",
        "{",
        "namespace wayland",
        "{",
        empty_line,
        List{interface_emitters, empty_line},
        empty_line,
        "}",
        "}",
        "}",
        empty_line,
        include_guard_bottom(include_guard_macro)
    };
}

Emitter source_file(std::string input_file_path, std::vector<Interface> const& interfaces)
{
    std::vector<Emitter> interface_emitters;
    for (auto const& interface : interfaces)
    {
        interface_emitters.push_back(interface.implementation());
    }

    std::string protocol_name = file_name_from_path(input_file_path);
    if (protocol_name.substr(protocol_name.size() - 4) == ".xml")
        protocol_name = protocol_name.substr(0, protocol_name.size() - 4);

    return Lines{
        comment_header(input_file_path),
        empty_line,
        impl_includes(protocol_name),
        empty_line,
        "namespace mfw = mir::frontend::wayland;",
        empty_line,
        "namespace",
        "{",
        {"struct wl_interface const* all_null_types [] {"},
            {List{std::vector<Emitter>(all_null_types_size, "nullptr"), Line{{","}, false, true}, Emitter::single_indent}, "};"},
        "}",
        empty_line,
        List{interface_emitters, empty_line},
    };
}

int main(int argc, char** argv)
{
    Emitter usage_emitter = Lines{
        empty_line,
        "/*",
        {"Usage: ./", file_name_from_path(argv[0]), " prefix header input mode"},
        Block{
            "prefix: the name prefix which will be removed, such as wl_",
            "        to not use a prefix, use _ or anything that won't match the start of a name",
            "input: the input xml file path",
            "mode: 'header' or 'source'",
        },
        "*/",
        empty_line,
    };

    if (argc != 4)
    {
        usage_emitter.emit({std::cerr});
        usage_emitter.emit({std::cout});
        exit(1);
    }

    std::string const prefix{argv[1]};
    std::string const input_file_path{argv[2]};
    bool header_mode{true};
    std::string mode_str = argv[3];
    if (mode_str == "header")
    {
        header_mode = true;
    }
    else if (mode_str == "source")
    {
        header_mode = false;
    }
    else
    {
        usage_emitter.emit({std::cerr});
        usage_emitter.emit({std::cout});
        exit(1);
    }

    auto name_transform = [prefix](std::string protocol_name)
    {
        std::string transformed_name = protocol_name;
        if (protocol_name.find(prefix) == 0) // if the first instance of prefix is at the start of protocol_name
        {
            // cut off the prefix
            transformed_name = protocol_name.substr(prefix.length());
        }
        return to_camel_case(transformed_name);
    };

    xmlpp::DomParser parser(input_file_path);

    auto document = parser.get_document();

    auto root_node = document->get_root_node();

    auto constructor_nodes = root_node->find("//arg[@type='new_id']");
    std::unordered_set<std::string> constructible_interfaces;
    for (auto const node : constructor_nodes)
    {
        auto arg = dynamic_cast<xmlpp::Element const*>(node);
        constructible_interfaces.insert(arg->get_attribute_value("interface"));
    }

    std::vector<Interface> interfaces;
    for (auto top_level : root_node->get_children("interface"))
    {
        auto interface = dynamic_cast<xmlpp::Element*>(top_level);

        if (interface->get_attribute_value("name") == "wl_display" ||
            interface->get_attribute_value("name") == "wl_registry")
        {
            // These are special, and don't need binding.
            continue;
        }
        interfaces.emplace_back(*interface, name_transform, constructible_interfaces);
    }

    Emitter emitter{nullptr};
    if (header_mode)
        emitter = header_file(input_file_path, interfaces);
    else
        emitter = source_file(input_file_path, interfaces);

    emitter.emit({std::cout});
}
