add_executable(Kernel
    src/main.cpp
    src/kinit.cpp
    src/process/process.cpp
    src/memory/pallocator.cpp
    src/memory/vallocator.cpp
    src/memory/kheap.cpp
    src/cpu/interrupts.cpp
    src/cpu/mp.cpp
    src/dbg/bugcheck.cpp
    src/crtstub.cpp
    src/perf/sysmetrics.cpp
    src/object/object.cpp
    src/dbg/kassert.cpp
    src/dbg/kasan.cpp
    src/process/taskScheduler.cpp
    src/device/pcie.cpp
    src/device/usb.cpp
    src/device/dma.cpp
    src/device/dpc.cpp
)
set(KERNEL_IMAGE "kernel")

target_link_libraries(Kernel PRIVATE utilitiesASAN BootVideo)

if(MSVC)
    # Remove runtime checks added by CMake in Debug mode
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTCc" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTCs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTCu" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/EHs" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/EHc" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/D_DEBUG" "" CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "-D_DEBUG" "" CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/MDd" "/MD" CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/MTd" "/MT" CMAKE_CXX_FLAGS_DEBUG
        "${CMAKE_CXX_FLAGS_DEBUG}")
    
    target_compile_options(Kernel PRIVATE
        /GS-
        /Oi
        /GL-
        /Gy
        /GR-
        /EHs-c-
        /D_HAS_EXCEPTIONS=0
        /D_ITERATOR_DEBUG_LEVEL=0
        /D_SECURE_SCL=0
        /D_NO_DEBUG_HEAP=1
        /DUNICODE
        /D_UNICODE
        /D_HAS_EXCEPTIONS=0
        /D_MSVC_STL_HARDENING=0
    )
    
    target_link_options(Kernel PRIVATE
        /SUBSYSTEM:NATIVE
        /ENTRY:KiStartup
        /NODEFAULTLIB
        /DLL
    )
    
    set_target_properties(Kernel PROPERTIES
        OUTPUT_NAME "kernel"
        SUFFIX ".exe"
        LINK_FLAGS "/SUBSYSTEM:NATIVE"
    )
    
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Kernel PRIVATE
        -ffreestanding
        -fshort-wchar
        -fno-exceptions
        -fno-rtti
        -fno-asynchronous-unwind-tables
        -fno-omit-frame-pointer
        -fsanitize=undefined -fsanitize=function -fsanitize=cfi -fstack-protector -fstack-protector-all -flto -fvisibility=hidden
        -D_GLIBCXX_DEBUG=0
        -D_GLIBCXX_ASSERTIONS=0
        -D_LIBCPP_ENABLE_ASSERTIONS=0
        -D_ITERATOR_DEBUG_LEVEL=0
        -D_MSVC_STL_HARDENING=0
        -D_HAS_EXCEPTIONS=0
        -D_SECURE_SCL=0
        -DUNICODE
        -D_UNICODE
    )
    
    if(EFI_ARCH STREQUAL "X64")
        target_compile_options(Kernel PRIVATE -m64 -mno-red-zone)
        target_link_options(Kernel PRIVATE -m64)
    elseif(EFI_ARCH STREQUAL "IA32")
        target_compile_options(Kernel PRIVATE -m32 -mno-red-zone)
        target_link_options(Kernel PRIVATE -m32)
    elseif(EFI_ARCH STREQUAL "AA64")
        # ARM64 specific options
    endif()
    
    target_link_options(Kernel PRIVATE
        -nostdlib
        -Wl,-dll
        -flto
        -fsanitize=cfi
        -shared
        -Wl,/subsystem:NATIVE
        -Wl,/ENTRY:KiStartup
        -Wl,/guard:cf
        -Wl,/NODEFAULTLIB
    )

    
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(Kernel PRIVATE -Xlinker /SAFESEH:NO)
    endif()

    
    set_target_properties(Kernel PROPERTIES
        OUTPUT_NAME ${KERNEL_IMAGE}
        SUFFIX ".exe"
    )
endif()

set_target_properties(Kernel PROPERTIES
    POSITION_INDEPENDENT_CODE OFF
)
execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    RESULT_VARIABLE _symlink_result
    ERROR_QUIET
)

message(STATUS "Building the kernel: ${KERNEL_IMAGE}.exe")
