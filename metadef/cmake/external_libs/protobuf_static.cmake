include(ExternalProject)
include(GNUInstallDirs)
#set(CMAKE_INSTALL_PREFIX ${GE_CODE_DIR}/output)

if ((${CMAKE_INSTALL_PREFIX} STREQUAL /usr/local) OR
    (${CMAKE_INSTALL_PREFIX} STREQUAL "C:/Program Files (x86)/ascend"))
    set(CMAKE_INSTALL_PREFIX ${GE_CODE_DIR}/output CACHE STRING "path for install()" FORCE)
    message(STATUS "No install prefix selected, default to ${CMAKE_INSTALL_PREFIX}.")
endif()

if (METADEF_PB_PKG)
    set(REQ_URL "${METADEF_PB_PKG}/libs/protobuf/v3.8.0.tar.gz")
else()
    if (ENABLE_GITEE)
        set(REQ_URL "https://gitee.com/mirrors/protobuf_source/repository/archive/v3.8.0.tar.gz")
        set(MD5 "eba86ae9f07ba5cfbaf8af3bc4e84236")
    else()
        set(REQ_URL "https://github.com/protocolbuffers/protobuf/archive/v3.8.0.tar.gz")
        set(MD5 "3d9e32700639618a4d2d342c99d4507a")
    endif ()
endif()

set(protobuf_CXXFLAGS "-Wno-maybe-uninitialized -Wno-unused-parameter -fPIC -fstack-protector-all -D_FORTIFY_SOURCE=2 -D_GLIBCXX_USE_CXX11_ABI=0 -O2 -Dgoogle=ascend_private")
set(protobuf_LDFLAGS "-Wl,-z,relro,-z,now,-z,noexecstack")
set(PROTOBUF_STATIC_PKG_DIR ${CMAKE_INSTALL_PREFIX}/protobuf_static)
ExternalProject_Add(protobuf_static_build
                    URL ${REQ_URL}
                    #URL /home/txd/workspace/linux_cmake/pkg/protobuf-3.8.0.tar.gz
                    #SOURCE_DIR ${METADEF_DIR}/../../third_party/protobuf/src/protobuf-3.8.0
                    CONFIGURE_COMMAND ${CMAKE_COMMAND}
                    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                    -DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}
                    -DCMAKE_LINKER=${CMAKE_LINKER}
                    -DCMAKE_AR=${CMAKE_AR}
                    -DCMAKE_RANLIB=${CMAKE_RANLIB}
                    -Dprotobuf_WITH_ZLIB=OFF
                    -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_CXX_FLAGS=${protobuf_CXXFLAGS} -DCMAKE_CXX_LDFLAGS=${protobuf_LDFLAGS} -DCMAKE_INSTALL_PREFIX=${PROTOBUF_STATIC_PKG_DIR} <SOURCE_DIR>/cmake
                    BUILD_COMMAND $(MAKE)
                    INSTALL_COMMAND $(MAKE) install
                    EXCLUDE_FROM_ALL TRUE
)
include(GNUInstallDirs)

add_library(ascend_protobuf_static_lib STATIC IMPORTED)

set_target_properties(ascend_protobuf_static_lib PROPERTIES
                      IMPORTED_LOCATION ${PROTOBUF_STATIC_PKG_DIR}/${CMAKE_INSTALL_LIBDIR}/libascend_protobuf.a
)

add_library(ascend_protobuf_static INTERFACE)
target_include_directories(ascend_protobuf_static INTERFACE ${PROTOBUF_STATIC_PKG_DIR}/include)
target_link_libraries(ascend_protobuf_static INTERFACE ascend_protobuf_static_lib)

add_dependencies(ascend_protobuf_static protobuf_static_build)
