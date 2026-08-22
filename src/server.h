#pragma once
#include "site_builder.h"
#include <string>
#include <atomic>

class DevServer {
public:
    DevServer(const fs::path& content_dir, const fs::path& output_dir,
              const fs::path& template_dir, int port = 8000);

    void run();
    void stop();

private:
    fs::path content_dir_;
    fs::path output_dir_;
    fs::path template_dir_;
    int port_;
    std::atomic<bool> running_;
    std::string empty_baseurl_;

    void build_site();
    pid_t start_server();
    void watch_and_rebuild();
};
