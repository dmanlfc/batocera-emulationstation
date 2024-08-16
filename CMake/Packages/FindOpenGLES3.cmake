# FindOpenGLES3
# ------------
# Finds the OpenGLES3 library

if(NOT HINT_GLES_LIBNAME)
    set(HINT_GLES_LIBNAME GLESv3)
endif()

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(OPENGLES3 glesv3)
    if(NOT OPENGLES3_FOUND)
        pkg_check_modules(OPENGLES3 glesv2)
        set(HINT_GLES_LIBNAME GLESv2)
    endif()
endif()

if(OPENGLES3_FOUND)
    set(OPENGLES3_INCLUDE_DIR ${OPENGLES3_INCLUDE_DIRS})
    set(OPENGLES3_gl_LIBRARY ${OPENGLES3_LIBRARIES})
else()
    find_path(OPENGLES3_INCLUDE_DIR GLES3/gl3.h
        PATHS "${CMAKE_FIND_ROOT_PATH}/usr/include"
        HINTS ${HINT_GLES_INCDIR}
    )

    if(NOT OPENGLES3_INCLUDE_DIR)
        find_path(OPENGLES3_INCLUDE_DIR GLES2/gl2.h
            PATHS "${CMAKE_FIND_ROOT_PATH}/usr/include"
            HINTS ${HINT_GLES_INCDIR}
        )
    endif()

    find_library(OPENGLES3_gl_LIBRARY
        NAMES ${HINT_GLES_LIBNAME}
        HINTS ${HINT_GLES_LIBDIR}
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenGLES3
            REQUIRED_VARS OPENGLES3_gl_LIBRARY OPENGLES3_INCLUDE_DIR)

if(OPENGLES3_FOUND)
    set(OPENGLES3_LIBRARIES ${OPENGLES3_gl_LIBRARY})
    set(OPENGLES3_INCLUDE_DIRS ${OPENGLES3_INCLUDE_DIR})
    mark_as_advanced(OPENGLES3_INCLUDE_DIR OPENGLES3_gl_LIBRARY)
endif()
