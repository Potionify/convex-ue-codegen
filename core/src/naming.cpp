#include <convex_codegen/naming.h>

#include <cctype>

namespace convex_codegen {

namespace {

bool is_alnum(unsigned char c) { return std::isalnum(c) != 0; }

}  // namespace

split_identifier split_module_function(std::string_view identifier) {
    split_identifier out;
    const std::size_t colon = identifier.find(':');
    std::string_view module_part;
    if (colon == std::string_view::npos) {
        module_part = identifier;
        out.function_name = "default";
    } else {
        module_part = identifier.substr(0, colon);
        out.function_name = std::string(identifier.substr(colon + 1));
        if (out.function_name.empty()) out.function_name = "default";
    }
    // Strip a single trailing ".js" bundler extension from the module part.
    if (module_part.size() >= 3 && module_part.substr(module_part.size() - 3) == ".js") {
        module_part = module_part.substr(0, module_part.size() - 3);
    }
    out.module_path = std::string(module_part);
    return out;
}

std::string canonicalize_identifier(std::string_view identifier) {
    const split_identifier s = split_module_function(identifier);
    return s.module_path + ":" + s.function_name;
}

std::string pascal_case(std::string_view segment) {
    std::string out;
    out.reserve(segment.size());
    bool cap_next = true;
    for (const char raw : segment) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (is_alnum(c)) {
            out.push_back(cap_next ? static_cast<char>(std::toupper(c)) : raw);
            cap_next = false;
        } else {
            cap_next = true;
        }
    }
    if (out.empty()) out = "_";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

std::vector<std::string> module_namespace_segments(std::string_view module_path) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= module_path.size()) {
        const std::size_t slash = module_path.find('/', start);
        const std::string_view raw = (slash == std::string_view::npos)
                                         ? module_path.substr(start)
                                         : module_path.substr(start, slash - start);
        if (!raw.empty()) segments.push_back(pascal_case(raw));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    if (segments.empty()) segments.push_back("_");
    return segments;
}

std::string module_flat(std::string_view module_path) {
    std::string out;
    for (const std::string& seg : module_namespace_segments(module_path)) out += seg;
    return out;
}

}  // namespace convex_codegen
