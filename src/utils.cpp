#include "utils.h"

namespace utils {

std::string read_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_file(const fs::path& path, const std::string& content) {
    ensure_dir(path.parent_path());
    std::ofstream f(path);
    f << content;
}

void ensure_dir(const fs::path& path) {
    if (!path.empty()) fs::create_directories(path);
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::string slugify(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (std::isalnum(c)) r += std::tolower(c);
        else if (c == ' ' || c == '-') r += '-';
        else if (c == '_') r += '_';
    }
    while (r.size() > 1 && r.back() == '-') r.pop_back();
    return r;
}

std::string date_format(const std::string& date_str, const std::string& fmt) {
    std::tm tm = {};
    std::string ds = date_str.size() >= 10 ? date_str.substr(0, 10) : date_str;
    std::stringstream ss(ds);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) return date_str;
    char buf[256];
    std::strftime(buf, sizeof(buf), fmt.c_str(), &tm);
    return buf;
}

std::string date_to_xmlschema(const std::string& date_str) {
    return date_format(date_str, "%Y-%m-%dT00:00:00Z");
}

std::string now_date(const std::string& fmt) {
    auto now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    char buf[256];
    std::strftime(buf, sizeof(buf), fmt.c_str(), &tm);
    return buf;
}

int word_count(const std::string& text) {
    std::istringstream iss(text);
    int count = 0;
    std::string w;
    while (iss >> w) count++;
    return count;
}

std::string strip_html(const std::string& html) {
    std::string r;
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') in_tag = true;
        else if (c == '>') in_tag = false;
        else if (!in_tag) r += c;
    }
    return r;
}

std::string escape_once(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r += c;
        }
    }
    return r;
}

std::string jsonify(const std::string& s) {
    return "\"" + escape_once(s) + "\"";
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

std::string replace(const std::string& s, const std::string& from, const std::string& to) {
    std::string r = s;
    size_t pos = 0;
    while ((pos = r.find(from, pos)) != std::string::npos) {
        r.replace(pos, from.length(), to);
        pos += to.length();
    }
    return r;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

std::string join(const std::vector<std::string>& v, const std::string& delim) {
    std::string r;
    for (size_t i = 0; i < v.size(); i++) {
        if (i > 0) r += delim;
        r += v[i];
    }
    return r;
}

std::string truncatewords(const std::string& s, int count) {
    std::istringstream iss(s);
    std::string r;
    std::string w;
    int n = 0;
    while (iss >> w && n < count) {
        if (n > 0) r += " ";
        r += w;
        n++;
    }
    if (iss >> w) r += "...";
    return r;
}

}
