
set(STC_EXT ${CMAKE_STATIC_LIBRARY_SUFFIX})
set(SRD_EXT ${CMAKE_SHARED_LIBRARY_SUFFIX})

if(NOT TARGET Lz4++::shared)
    add_library(Lz4++::shared SHARED IMPORTED)
    set_target_properties(Lz4++::shared PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${Lz4++_INCLUDE_DIRS}
        IMPORTED_IMPLIB_${BUILD_TYPE} ${Lz4++_LIB_PATH}/lz4++${STC_EXT}
        IMPORTED_LOCATION_${BUILD_TYPE} ${Lz4++_BIN_PATH}/lz4++${SRD_EXT}
    )
endif()

if(ENABLED_STATIC_LZ4XX)
    if(NOT TARGET Lz4++::static)
        add_library(Lz4++::static STATIC IMPORTED)
        set_target_properties(Lz4++::static PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES ${Lz4++_INCLUDE_DIRS}
            IMPORTED_LOCATION_${BUILD_TYPE} ${Lz4++_LIB_PATH}/lz4++_static${STC_EXT}
        )
    endif()
endif()

unset(STC_EXT)
unset(SRD_EXT)
