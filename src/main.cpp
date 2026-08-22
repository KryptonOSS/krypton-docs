#include "site_builder.h"
#include "server.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    fs::path content_dir = "content";
    fs::path output_dir = "public";
    fs::path template_dir = "templates";
    bool serve_mode = false;
    int port = 8000;

    int i = 1;
    if (i < argc && std::string(argv[i]) == "serve") {
        serve_mode = true;
        i++;
    }

    for (; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--content" && i + 1 < argc) content_dir = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_dir = argv[++i];
        else if (arg == "--templates" && i + 1 < argc) template_dir = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            std::cout << "pagepp - Static Site Generator\n\n"
                      << "Usage:\n"
                      << "  pagepp [options]              Build the site\n"
                      << "  pagepp serve [options]        Start dev server with live rebuild\n\n"
                      << "Options:\n"
                      << "  --content <dir>    Content directory (default: content)\n"
                      << "  --output <dir>     Output directory (default: public)\n"
                      << "  --templates <dir>  Templates directory (default: templates)\n"
                      << "  --port <port>      Dev server port (default: 8000)\n";
            return 0;
        }
    }

    if (serve_mode) {
        DevServer server(content_dir, output_dir, template_dir, port);
        server.run();
        return 0;
    }

    std::cout << "Content:  " << content_dir << "\n"
              << "Output:   " << output_dir << "\n"
              << "Template: " << template_dir << "\n" << std::endl;

    SiteBuilder builder(content_dir, output_dir, template_dir);
    builder.build();

    return 0;
}
