#include "server.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <vector>
#include <algorithm>

static DevServer* g_server = nullptr;

static void signal_handler(int sig) {
    if (g_server) {
        g_server->stop();
    }
}

DevServer::DevServer(const fs::path& content_dir, const fs::path& output_dir,
                     const fs::path& template_dir, int port)
    : content_dir_(content_dir), output_dir_(output_dir), template_dir_(template_dir),
      port_(port), running_(false) {
    g_server = this;
}

void DevServer::stop() {
    running_ = false;
}

void DevServer::build_site() {
    SiteBuilder builder(content_dir_, output_dir_, template_dir_, &empty_baseurl_);
    builder.build();
}

int DevServer::start_server() {
    pid_t pid = fork();
    if (pid == 0) {
        std::string port_str = std::to_string(port_);
        std::string dir_str = output_dir_.string();
        execlp("python3", "python3", "-m", "http.server",
               port_str.c_str(), "--directory", dir_str.c_str(), nullptr);
        execlp("python", "python", "-m", "http.server",
               port_str.c_str(), "--directory", dir_str.c_str(), nullptr);
        std::cerr << "Error: python3/python not found. Install Python to use the dev server." << std::endl;
        _exit(1);
    }
    return pid;
}

void DevServer::watch_and_rebuild() {
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        std::cerr << "Error: inotify not available" << std::endl;
        return;
    }

    std::vector<std::pair<int, std::string>> watches;

    auto add_watch = [&](const fs::path& dir, const std::string& label) {
        if (!fs::exists(dir)) return;
        int wd = inotify_add_watch(inotify_fd, dir.string().c_str(),
                                   IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);
        if (wd >= 0) {
            watches.push_back({wd, label});
        }
    };

    add_watch(content_dir_, "content");
    add_watch(template_dir_, "templates");

    fs::path assets_dir = content_dir_.parent_path() / "assets";
    add_watch(assets_dir, "assets");

    for (auto& sec_entry : fs::directory_iterator(content_dir_)) {
        if (sec_entry.is_directory()) {
            add_watch(sec_entry.path(), sec_entry.path().filename().string());
        }
    }

    const size_t EVENT_SIZE = sizeof(struct inotify_event);
    const size_t BUF_LEN = 1024 * (EVENT_SIZE + 16);
    char buffer[BUF_LEN];

    running_ = true;
    std::cout << "Watching for changes... (Ctrl+C to stop)" << std::endl;

    auto last_build = std::chrono::steady_clock::now();
    bool needs_rebuild = false;

    while (running_) {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(inotify_fd, &fds);

        int select_result = select(inotify_fd + 1, &fds, nullptr, nullptr, &tv);

        if (select_result > 0 && FD_ISSET(inotify_fd, &fds)) {
            int length = read(inotify_fd, buffer, BUF_LEN);
            if (length > 0) {
                needs_rebuild = true;
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_build).count();

        if (needs_rebuild && elapsed > 500) {
            std::cout << "\nRebuilding..." << std::endl;
            build_site();
            std::cout << "Watching for changes... (Ctrl+C to stop)" << std::endl;
            last_build = now;
            needs_rebuild = false;
        }
    }

    for (auto& [wd, label] : watches) {
        inotify_rm_watch(inotify_fd, wd);
    }
    close(inotify_fd);
}

void DevServer::run() {
    std::cout << "Building site..." << std::endl;
    build_site();

    int server_pid = start_server();
    std::cout << "\n  Server running at http://localhost:" << port_ << "/\n" << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    watch_and_rebuild();

    std::cout << "\nStopping server..." << std::endl;
    kill(server_pid, SIGTERM);
    waitpid(server_pid, nullptr, 0);

    g_server = nullptr;
}
