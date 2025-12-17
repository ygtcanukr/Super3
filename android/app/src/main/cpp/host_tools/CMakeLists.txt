cmake_minimum_required(VERSION 3.10)
project(m68kmake_host_builder C)

# Find the root of the repository to locate the source file.
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../../" ABSOLUTE)

# Add the executable from its location in the source tree.
add_executable(m68kmake_host "${REPO_ROOT}/Src/CPU/68K/Musashi/m68kmake.c")

# Define where to install the built executable (e.g., into <build_dir>/host_install/bin)
include(GNUInstallDirs)
install(TARGETS m68kmake_host DESTINATION ${CMAKE_INSTALL_BINDIR})
