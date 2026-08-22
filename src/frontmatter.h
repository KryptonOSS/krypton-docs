#pragma once
#include "utils.h"
#include <toml++/toml.hpp>

struct FrontMatter {
    toml::table meta;
    std::string body;
    Dict flat;

    std::string get_str(const std::string& key, const std::string& def = "") const {
        if (auto val = meta[key].as_string()) return std::string(val->get());
        if (auto val = meta[key].as_boolean()) return val->get() ? "true" : "false";
        return def;
    }
    bool get_bool(const std::string& key, bool def = false) const {
        if (auto val = meta[key].as_boolean()) return val->get();
        return def;
    }
    bool has(const std::string& key) const {
        return meta.contains(key);
    }
    std::vector<std::string> get_array(const std::string& key) const {
        std::vector<std::string> result;
        if (auto arr = meta[key].as_array()) {
            for (auto& item : *arr) {
                if (auto s = item.as_string()) result.push_back(std::string(s->get()));
            }
        }
        return result;
    }
};

FrontMatter parse_frontmatter(const std::string& raw);
