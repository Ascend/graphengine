include(ExternalProject)
include(GNUInstallDirs)
#set(CMAKE_INSTALL_PREFIX ${GE_CODE_DIR}/output)

if ((${CMAKE_INSTALL_PREFIX} STREQUAL /usr/local) OR
    (${CMAKE_INSTALL_PREFIX} STREQUAL "C:/Program Files (x86)/ascend"))
    set(CMAKE_INSTALL_PREFIX ${GE_CODE_DIR}/output CACHE STRING "path for install()" FORCE)
    message(STATUS "No install prefix selected, default to ${CMAKE_INSTALL_PREFIX}.")
endif()

set(protobuf_CXXFLAGS "-Wno-maybe-uninitialized -Wno-unused-parameter -fPIC -fstack-protector-all -D_FORTIFY_SOURCE=2 -D_GLIBCXX_USE_CXX11_ABI=0 -O2")
set(protobuf_LDFLAGS "-Wl,-z,relro,-z,now,-z,noexecstack")
set(PROTOBUF_STATIC_PKG_DIR ${CMAKE_INSTALL_PREFIX}/protobuf_static)
ExternalProject_Add(protobuf_static_build
                    URL https://github.com/protocolbuffers/protobuf/archive/v3.8.0.tar.gz
                    #URL /home/txd/workspace/linux_cmake/pkg/protobuf-3.8.0.tar.gz
                    SOURCE_DIR ${GE_CODE_DIR}/../third_party/protobuf/src/protobuf-3.8.0
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

add_library(protobuf_static_lib STATIC IMPORTED)

set_target_properties(protobuf_static_lib PROPERTIES
                      IMPORTED_LOCATION ${PROTOBUF_STATIC_PKG_DIR}/${CMAKE_INSTALL_LIBDIR}/libprotobuf.a
)

add_library(protobuf_static INTERFACE)
target_include_directories(protobuf_static INTERFACE ${PROTOBUF_STATIC_PKG_DIR}/include)
target_link_libraries(protobuf_static INTERFACE protobuf_static_lib)

add_dependencies(protobuf_static protobuf_static_build)
