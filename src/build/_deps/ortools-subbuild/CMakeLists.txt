# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.22.1)

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(ortools-populate NONE)


# Pass through things we've already detected in the main project to avoid
# paying the cost of redetecting them again in ExternalProject_Add()
set(GIT_EXECUTABLE [==[/usr/bin/git]==])
set(GIT_VERSION_STRING [==[2.34.1]==])
set_property(GLOBAL PROPERTY _CMAKE_FindGit_GIT_EXECUTABLE_VERSION
  [==[/usr/bin/git;2.34.1]==]
)


include(ExternalProject)
ExternalProject_Add(ortools-populate
                     "UPDATE_DISCONNECTED" "False" "GIT_REPOSITORY" "https://github.com/google/or-tools.git" "GIT_TAG" "v9.13" "PATCH_COMMAND" "cp" "-rf" "/home/alzoobiali/Desktop/RTE/Antares/cloneAntares/AntaresADQPDev/src/build/_deps/ortools-rte-src/bzip2.patch" "/home/alzoobiali/Desktop/RTE/Antares/cloneAntares/AntaresADQPDev/src/build/_deps/ortools-rte-src/patch.py" "/home/alzoobiali/Desktop/RTE/Antares/cloneAntares/AntaresADQPDev/src/build/_deps/ortools-rte-src/patch_utils.py" "/home/alzoobiali/Desktop/RTE/Antares/cloneAntares/AntaresADQPDev/src/build/_deps/ortools-rte-src/ortools" "/home/alzoobiali/Desktop/RTE/Antares/cloneAntares/AntaresADQPDev/src/build/_deps/ortools-rte-src/cmake_patches" "." "&&" "/usr/bin/python3.10" "patch.py" "OVERRIDE_FIND_PACKAGE" "ON"
                    SOURCE_DIR          "/home/alzoobiali/Desktop/RTE/Antares/cloneAntares/AntaresADQPDev/src/build/_deps/ortools-src"
                    BINARY_DIR          "/home/alzoobiali/Desktop/RTE/Antares/cloneAntares/AntaresADQPDev/src/build/_deps/ortools-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
)


