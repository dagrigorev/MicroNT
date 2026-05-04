cmake_minimum_required(VERSION 3.20)
project(MicroNT LANGUAGES NONE)

# The kernel sub-project uses its own toolchain.
# User-mode sub-projects (user/, tools/) use the host MSVC/Clang.

add_subdirectory(kernel)
