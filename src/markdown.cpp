#include "markdown.h"
#include <cmark.h>

static std::string add_heading_ids(const std::string& html) {
    std::string result;
    result.reserve(html.size() + 1024);
    size_t pos = 0;

    while (pos < html.size()) {
        auto tag_start = html.find("<h", pos);
        if (tag_start == std::string::npos) {
            result += html.substr(pos);
            break;
        }
        result += html.substr(pos, tag_start - pos);

        auto tag_close = html.find(">", tag_start);
        if (tag_close == std::string::npos) {
            result += html.substr(tag_start);
            break;
        }

        std::string tag = html.substr(tag_start, tag_close - tag_start + 1);

        if (tag.find("id=") == std::string::npos) {
            auto close_tag = tag.find('>');
            std::string heading_num;
            if (tag.size() >= 4 && tag[2] >= '1' && tag[2] <= '6' && tag[3] == '>')
                heading_num = std::string(1, tag[2]);
            else if (tag.size() >= 3 && tag[2] >= '1' && tag[2] <= '6')
                heading_num = std::string(1, tag[2]);

            if (!heading_num.empty()) {
                auto text_end = html.find("</h" + heading_num, tag_close + 1);
                if (text_end != std::string::npos) {
                    std::string heading_text = html.substr(tag_close + 1, text_end - tag_close - 1);
                    std::string stripped = utils::strip_html(heading_text);
                    std::string slug = utils::slugify(utils::trim(stripped));
                    tag.insert(tag_close - tag_start, " id=\"" + slug + "\"");
                }
            }
        }
        result += tag;
        pos = tag_close + 1;
    }
    return result;
}

std::string markdown_to_html(const std::string& md) {
    if (md.empty()) return "";
    char* html = cmark_markdown_to_html(
        md.c_str(), md.size(),
        CMARK_OPT_DEFAULT | CMARK_OPT_SMART | CMARK_OPT_UNSAFE
    );
    std::string raw(html);
    free(html);
    return add_heading_ids(raw);
}
