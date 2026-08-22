#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <fstream>

namespace fs = std::filesystem;

using Dict = std::map<std::string, std::string>;
using DictList = std::vector<Dict>;

namespace utils {

std::string read_file(const fs::path& path);
void write_file(const fs::path& path, const std::string& content);
void ensure_dir(const fs::path& path);
std::string trim(const std::string& s);
std::string to_lower(const std::string& s);
std::string slugify(const std::string& s);
std::string date_format(const std::string& date_str, const std::string& fmt);
std::string date_to_xmlschema(const std::string& date_str);
std::string now_date(const std::string& fmt);
int word_count(const std::string& text);
std::string strip_html(const std::string& html);
std::string escape_once(const std::string& s);
std::string jsonify(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
std::string replace(const std::string& s, const std::string& from, const std::string& to);
bool starts_with(const std::string& s, const std::string& prefix);
bool contains(const std::string& s, const std::string& sub);
std::string join(const std::vector<std::string>& v, const std::string& delim);
std::string truncatewords(const std::string& s, int count);

}
