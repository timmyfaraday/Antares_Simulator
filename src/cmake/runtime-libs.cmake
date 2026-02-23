# Helper module to collect runtime libraries (DLLs) on Windows and expose
# - SYSTEM_RUNTIME_LIBS (list of full paths)
# - SYSTEM_RUNTIME_LIBS_STR (quoted space-separated string for NSIS/CPack templates)
# It will also install found DLLs into the install tree (dest: bin) so CPack
# includes them in generated archives.

if (WIN32)
    set(_found_dlls_list "")

    function(_append_if_exists path)
        if (EXISTS "${path}")
            file(GLOB _tmp_dlls RELATIVE "${path}" "${path}/*.dll")
            foreach(_d IN LISTS _tmp_dlls)
                list(APPEND _found_dlls_list "${path}/${_d}")
            endforeach()
        endif()
    endfunction()

    # 1) Try conventional vcpkg installed directory from VCPKG_ROOT
    if (DEFINED VCPKG_ROOT)
        if (DEFINED VCPKG_TARGET_TRIPLET)
            set(_vcpkg_installed_dirs
                "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin"
                "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/debug/bin"
            )
        else()
            set(_vcpkg_installed_dirs
                "${VCPKG_ROOT}/installed/x64-windows/bin"
                "${VCPKG_ROOT}/installed/x64-windows/debug/bin"
            )
        endif()
        foreach(_d IN LISTS _vcpkg_installed_dirs)
            _append_if_exists(${_d})
        endforeach()
    endif()

    # 2) Common CI/build layout: ${CMAKE_BINARY_DIR}/vcpkg_installed/<triplet>/bin
    if (DEFINED VCPKG_TARGET_TRIPLET)
        set(_ci_vcpkg_bin "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin")
        _append_if_exists(${_ci_vcpkg_bin})
        # also consider release-specific naming convention (e.g. x64-windows-release)
        string(REPLACE "/" "\\/" _tmptrip ${VCPKG_TARGET_TRIPLET})
    endif()

    # try to glob any vcpkg_installed/*/bin under build dir
    file(GLOB _maybe_vcpkg_installed_dirs RELATIVE "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/vcpkg_installed/*/bin" )
    foreach(_rel IN LISTS _maybe_vcpkg_installed_dirs)
        _append_if_exists("${CMAKE_BINARY_DIR}/${_rel}")
    endforeach()

    # 3) Fallback: scan top-level vcpkg/installed/*/bin if the repo contains vcpkg submodule
    if (EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/installed")
        file(GLOB _vcpkg_installed_dirs_src RELATIVE "${CMAKE_SOURCE_DIR}/vcpkg/installed" "${CMAKE_SOURCE_DIR}/vcpkg/installed/*/bin")
        foreach(_rel IN LISTS _vcpkg_installed_dirs_src)
            _append_if_exists("${CMAKE_SOURCE_DIR}/vcpkg/installed/${_rel}")
        endforeach()
    endif()

    # 4) Also include any DLLs already present in build's install/bin (useful for local install tree)
    _append_if_exists("${CMAKE_CURRENT_BINARY_DIR}/install/bin")
    _append_if_exists("${CMAKE_BINARY_DIR}/install/bin")

    # Remove duplicates and filter non-existing
    list(REMOVE_DUPLICATES _found_dlls_list)
    set(SYSTEM_RUNTIME_LIBS "")
    foreach(_p IN LISTS _found_dlls_list)
        if (EXISTS "${_p}")
            list(APPEND SYSTEM_RUNTIME_LIBS "${_p}")
        endif()
    endforeach()

    # Install each found DLL into install/bin so CPack will include them
    foreach(_dll_path IN LISTS SYSTEM_RUNTIME_LIBS)
        install(FILES "${_dll_path}" DESTINATION bin OPTIONAL)
    endforeach()

    # Build quoted, space-separated string for NSIS templates or configure_file substitution
    set(SYSTEM_RUNTIME_LIBS_STR "")
    foreach(_dll_path IN LISTS SYSTEM_RUNTIME_LIBS)
        # NSIS likes backslashes for Windows paths
        string(REPLACE "/" "\\\\" _winpath "${_dll_path}")
        if(SYSTEM_RUNTIME_LIBS_STR STREQUAL "")
            set(SYSTEM_RUNTIME_LIBS_STR "\"${_winpath}\"")
        else()
            set(SYSTEM_RUNTIME_LIBS_STR "${SYSTEM_RUNTIME_LIBS_STR} \"${_winpath}\"")
        endif()
    endforeach()

    # Expose variable in the parent scope
    set(SYSTEM_RUNTIME_LIBS "${SYSTEM_RUNTIME_LIBS}" PARENT_SCOPE)
    set(SYSTEM_RUNTIME_LIBS_STR "${SYSTEM_RUNTIME_LIBS_STR}" PARENT_SCOPE)

    message(STATUS "Packaging: discovered runtime DLLs: ${SYSTEM_RUNTIME_LIBS_STR}")

    # --- DEBUG INFO: write a detailed log in the build directory to help packaging investigations ---
    # This log lists scanned candidate directories, number of DLLs found per directory and the final list.
    set(_runtime_debug_log "${CMAKE_BINARY_DIR}/runtime-libs-packaging.log")

    file(WRITE "${_runtime_debug_log}" "Runtime libs packaging debug log\n")
    file(APPEND "${_runtime_debug_log}" "Scanned candidate directories:\n")

    # Recompute candidate directories list for reporting (the same order as above)
    set(_candidate_dirs "")
    if (DEFINED VCPKG_ROOT)
        foreach(_d IN LISTS _vcpkg_installed_dirs)
            list(APPEND _candidate_dirs "${_d}")
        endforeach()
    endif()
    if (DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND _candidate_dirs "${_ci_vcpkg_bin}")
    endif()
    foreach(_rel IN LISTS _maybe_vcpkg_installed_dirs)
        list(APPEND _candidate_dirs "${CMAKE_BINARY_DIR}/${_rel}")
    endforeach()
    if (EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/installed")
        foreach(_rel IN LISTS _vcpkg_installed_dirs_src)
            list(APPEND _candidate_dirs "${CMAKE_SOURCE_DIR}/vcpkg/installed/${_rel}")
        endforeach()
    endif()
    list(APPEND _candidate_dirs "${CMAKE_CURRENT_BINARY_DIR}/install/bin")
    list(APPEND _candidate_dirs "${CMAKE_BINARY_DIR}/install/bin")

    foreach(_cd IN LISTS _candidate_dirs)
        if (EXISTS "${_cd}")
            file(GLOB _cd_dlls RELATIVE "${_cd}" "${_cd}/*.dll")
            list(LENGTH _cd_dlls _count)
            file(APPEND "${_runtime_debug_log}" " - ${_cd} -> ${_count} dll(s)\n")
            if (NOT _count EQUAL 0)
                foreach(_f IN LISTS _cd_dlls)
                    file(APPEND "${_runtime_debug_log}" "    * ${_cd}/${_f}\n")
                endforeach()
            endif()
        else()
            file(APPEND "${_runtime_debug_log}" " - ${_cd} -> (not found)\n")
        endif()
    endforeach()

    file(APPEND "${_runtime_debug_log}" "\nFinal SYSTEM_RUNTIME_LIBS list (${SYSTEM_RUNTIME_LIBS_STR}):\n")
    foreach(_p IN LISTS SYSTEM_RUNTIME_LIBS)
        file(APPEND "${_runtime_debug_log}" " * ${_p}\n")
    endforeach()

    message(STATUS "Packaging debug log written to: ${_runtime_debug_log}")
else()
    # Not WIN32: write a small note so packaging runs on non-Windows hosts are explicit
    message(STATUS "Packaging: runtime-libs helper skipped because this is not a Windows host (CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'). No Windows runtime DLLs will be collected.")

    set(_runtime_debug_log "${CMAKE_BINARY_DIR}/runtime-libs-packaging.log")
    file(WRITE "${_runtime_debug_log}" "Runtime libs packaging debug log - skipped (not WIN32)\n")
    file(APPEND "${_runtime_debug_log}" "CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}\n")
    if(DEFINED VCPKG_ROOT)
        file(APPEND "${_runtime_debug_log}" "VCPKG_ROOT=${VCPKG_ROOT}\n")
    endif()
    if(DEFINED VCPKG_TARGET_TRIPLET)
        file(APPEND "${_runtime_debug_log}" "VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}\n")
    endif()
    message(STATUS "Packaging debug note written to: ${_runtime_debug_log}")
endif()
