# include paths for qrenderdoc, the internal renderdoc API, and Python
set(MODULE_INCLUDES
    PRIVATE ${CMAKE_SOURCE_DIR}/qrendertest
    PRIVATE ${CMAKE_SOURCE_DIR}/rendertest/api/replay
    PRIVATE ${PYTHON_INCLUDE_DIR})
  
# preprocessor definitions for compiling
set(MODULE_DEFINES
    PRIVATE -DRENDERTEST_PLATFORM_LINUX)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-warning-option -Wno-unused-private-field -Wno-shadow -Wno-shorten-64-to-32")

    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 19)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-function-type")
    endif()
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-function-type -Wno-stringop-truncation")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")

# Set up rpath to find librendertest.so
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
if (APPLE)
    set(CMAKE_MACOSX_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH "@loader_path/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR TRUE)
else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN/:$ORIGIN/../lib${LIB_SUFFIX}/${LIB_SUBFOLDER_TRAIL_SLASH}")
endif()

# Add python library
set (CMAKE_SHARED_LINKER_FLAGS "${PYTHON_LIBRARY} ${CMAKE_SHARED_LINKER_FLAGS}")

add_library(_rendertest SHARED
    ${CMAKE_BINARY_DIR}/qrendertest/rendertest_python.cxx
    pyrendertest_stub.cpp)

set_source_files_properties(${CMAKE_BINARY_DIR}/qrendertest/rendertest_python.cxx
                            PROPERTIES GENERATED TRUE)

target_compile_definitions(_rendertest ${MODULE_DEFINES})
target_include_directories(_rendertest ${MODULE_INCLUDES})
target_link_libraries(_rendertest PRIVATE rendertest)

add_library(_qrendertest SHARED
    ${CMAKE_BINARY_DIR}/qrendertest/rendertest_python.cxx
    ${CMAKE_BINARY_DIR}/qrendertest/qrendertest_python.cxx
    pyrendertest_stub.cpp
    qrendertest_stub.cpp)

set_source_files_properties(${CMAKE_BINARY_DIR}/qrendertest/qrendertest_python.cxx
                            PROPERTIES GENERATED TRUE)

target_compile_definitions(_qrendertest ${MODULE_DEFINES})
target_include_directories(_qrendertest ${MODULE_INCLUDES})
target_link_libraries(_qrendertest PRIVATE rendertest)

# Python expects .so extension when importing shared libraries on non-Windows platform
if(APPLE)
    set_target_properties(_rendertest PROPERTIES SUFFIX ".so")
    set_target_properties(_qrendertest PROPERTIES SUFFIX ".so")
endif()

# Don't prefix with lib, python expects a bare .so.
# Also rename to non-underscore due to this (We couldn't call the
# python library 'rendertest' since that would clash with the
# prefixed target)
set_target_properties(_rendertest PROPERTIES PREFIX "")
set_target_properties(_qrendertest PROPERTIES PREFIX "")
set_target_properties(_rendertest PROPERTIES OUTPUT_NAME "rendertest")
set_target_properties(_qrendertest PROPERTIES OUTPUT_NAME "qrendertest")

# Make sure we build after the wrappers are generated
add_dependencies(_rendertest swig-bindings)
add_dependencies(_qrendertest swig-bindings)