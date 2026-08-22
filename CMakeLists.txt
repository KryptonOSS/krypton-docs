cmake_minimum_required(VERSION 3.14)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(pagepp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  cmark
  GIT_REPOSITORY https://github.com/commonmark/cmark.git
  GIT_TAG        0.31.1
)

FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG        v3.4.0
)

FetchContent_MakeAvailable(cmark tomlplusplus)

add_executable(pagepp
  src/main.cpp
  src/config.cpp
  src/frontmatter.cpp
  src/markdown.cpp
  src/template_engine.cpp
  src/site_builder.cpp
  src/server.cpp
  src/collections.cpp
  src/utils.cpp
)

target_include_directories(pagepp PRIVATE src)
target_link_libraries(pagepp PRIVATE cmark tomlplusplus::tomlplusplus)

install(TARGETS pagepp DESTINATION bin)
