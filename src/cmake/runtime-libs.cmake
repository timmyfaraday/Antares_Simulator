# Helper module to collect runtime libraries (DLLs) on Windows and expose
# - SYSTEM_RUNTIME_LIBS (list of full paths)
# - SYSTEM_RUNTIME_LIBS_STR (quoted space-separated string for NSIS/CPack templates)
# It will also install found DLLs into the install tree (dest: bin) so CPack
# includes them in generated archives.

# Gather runtime DLLs from vcpkg installed/<triplet>/bin if VCPKG_ROOT is defined.
if (WIN32)
    set(SYSTEM_RUNTIME_LIBS "")

    # Primary source: vcpkg installed bin folder
    if (DEFINED VCPKG_ROOT)
        if (DEFINED VCPKG_TARGET_TRIPLET)
            set(_vcpkg_bin_dir "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
        else()
            set(_vcpkg_bin_dir "${VCPKG_ROOT}/installed/x64-windows/bin")
        endif()

        if (EXISTS "${_vcpkg_bin_dir}")
            file(GLOB _vcpkg_dlls RELATIVE "${_vcpkg_bin_dir}" "${_vcpkg_bin_dir}/*.dll")
            foreach(_dll IN LISTS _vcpkg_dlls)
                list(APPEND SYSTEM_RUNTIME_LIBS "${_vcpkg_bin_dir}/${_dll}")
            endforeach()
        endif()
    endif()

    # Fallback: scan install/bin produced by the build (if any)
    if (EXISTS "${CMAKE_CURRENT_BINARY_DIR}/install/bin")
        file(GLOB _inst_dlls RELATIVE "${CMAKE_CURRENT_BINARY_DIR}/install/bin" "${CMAKE_CURRENT_BINARY_DIR}/install/bin/*.dll")
        foreach(_dll IN LISTS _inst_dlls)
            list(APPEND SYSTEM_RUNTIME_LIBS "${CMAKE_CURRENT_BINARY_DIR}/install/bin/${_dll}")
        endforeach()
    endif()

    # Remove duplicates
    list(REMOVE_DUPLICATES SYSTEM_RUNTIME_LIBS)

    # Install each found DLL into install/bin so CPack will include them
    foreach(_dll_path IN LISTS SYSTEM_RUNTIME_LIBS)
        if (EXISTS "${_dll_path}")
            install(FILES "${_dll_path}" DESTINATION bin OPTIONAL)
        endif()
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

    message(STATUS "Packaging: discovered ${SYSTEM_RUNTIME_LIBS_STR}")
endif()

