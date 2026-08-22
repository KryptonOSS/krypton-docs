#pragma once
#include "utils.h"
#include <toml++/toml.hpp>

struct SiteConfig {
    std::string title;
    std::string name;
    std::string description;
    std::string url;
    std::string baseurl;
    std::string locale;

    Dict social;
    DictList navigation;
};

SiteConfig load_config(const fs::path& config_path);
