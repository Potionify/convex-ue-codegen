#pragma once

// Identifier canonicalization and C++/Blueprint name derivation. Pure string
// transforms with no dependency on the JSON layer, so they are unit-testable
// in isolation. Everything here is deterministic: identical input always
// yields identical output, which the byte-exact golden tests rely on.

#include <string>
#include <string_view>
#include <vector>

namespace convex_codegen {

/// Split an apiSpec identifier ("counters.js:get", "dir/mod.js:fn") into its
/// module and function halves. The trailing ".js" of the module is stripped
/// (the bundler extension), and a missing function part becomes "default".
struct split_identifier {
    std::string module_path;    ///< canonical module path, e.g. "dir/mod"
    std::string function_name;  ///< function export, e.g. "fn"
};
split_identifier split_module_function(std::string_view identifier);

/// Canonicalize an identifier: strip the module ".js" extension, keeping the
/// "module:function" shape. "counters.js:get" -> "counters:get".
std::string canonicalize_identifier(std::string_view identifier);

/// PascalCase a single path/name segment. Non-[A-Za-z0-9] characters are
/// dropped and the following character is capitalized; a leading digit gets a
/// '_' prefix; an empty result becomes "_". "foo_bar" -> "FooBar",
/// "echoQuery" -> "EchoQuery", "2fa" -> "_2fa".
std::string pascal_case(std::string_view segment);

/// The nested-namespace segments for a canonical module path, each PascalCased.
/// "admin/tools" -> {"Admin", "Tools"}.
std::vector<std::string> module_namespace_segments(std::string_view module_path);

/// The flattened Blueprint class-name fragment: the namespace segments
/// concatenated. "admin/tools" -> "AdminTools".
std::string module_flat(std::string_view module_path);

}  // namespace convex_codegen
