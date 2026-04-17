if(MSVC)
    if(DETECTED_PROCESSOR MATCHES "ARM64|aarch64|AARCH64")
        enable_language(ASM_MARMASM)
    else()
        enable_language(ASM_MASM)
    endif()
else()
    enable_language(ASM)
endif()

set(UTILITIES_SOURCES
    source/operations.cpp
    source/memory.cpp
    source/paging.cpp
    source/cpu.cpp
)

if(DETECTED_PROCESSOR MATCHES "ARM64|aarch64|AARCH64")
    if(MSVC)
        list(APPEND UTILITIES_SOURCES source/exceptions_arm64.asm)
    else()
        list(APPEND UTILITIES_SOURCES source/exceptions_arm64.S)
        set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -target aarch64-unknown-windows-msvc")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64-bit x86
    if(MSVC)
        list(APPEND UTILITIES_SOURCES source/interrupts_x64.asm)
    else()
        list(APPEND UTILITIES_SOURCES source/interrupts_x64.s)
        set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -m64")
    endif()
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32-bit x86
    if(MSVC)
        list(APPEND UTILITIES_SOURCES source/interrupts_x86.asm)
    else()
        list(APPEND UTILITIES_SOURCES source/interrupts_x86.s)
        set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -m32")
    endif()
endif()

add_library(utilitiesASAN STATIC ${UTILITIES_SOURCES})
add_library(utilitiesBOOT STATIC ${UTILITIES_SOURCES})

target_include_directories(utilitiesASAN PUBLIC include)
target_include_directories(utilitiesBOOT PUBLIC include)

if(MSVC)
    target_compile_options(utilitiesASAN PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/GS->
        $<$<COMPILE_LANGUAGE:CXX>:/GR->
        $<$<COMPILE_LANGUAGE:CXX>:/EHs->
        $<$<COMPILE_LANGUAGE:CXX>:/EHc->
        $<$<COMPILE_LANGUAGE:CXX>:/D_HAS_EXCEPTIONS=0>
        $<$<COMPILE_LANGUAGE:CXX>:/D_ITERATOR_DEBUG_LEVEL=0>
        $<$<COMPILE_LANGUAGE:CXX>:/D_SECURE_SCL=0>
        $<$<COMPILE_LANGUAGE:CXX>:/D_NO_DEBUG_HEAP=1>
        $<$<COMPILE_LANGUAGE:CXX>:/Oi>
    )
    target_compile_options(utilitiesBOOT PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:/GS->
        $<$<COMPILE_LANGUAGE:CXX>:/GR->
        $<$<COMPILE_LANGUAGE:CXX>:/EHs->
        $<$<COMPILE_LANGUAGE:CXX>:/EHc->
        $<$<COMPILE_LANGUAGE:CXX>:/D_HAS_EXCEPTIONS=0>
        $<$<COMPILE_LANGUAGE:CXX>:/D_ITERATOR_DEBUG_LEVEL=0>
        $<$<COMPILE_LANGUAGE:CXX>:/D_SECURE_SCL=0>
        $<$<COMPILE_LANGUAGE:CXX>:/D_NO_DEBUG_HEAP=1>
        $<$<COMPILE_LANGUAGE:CXX>:/Oi>
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(utilitiesASAN PRIVATE
        -fno-exceptions
        -fno-rtti
        -fno-omit-frame-pointer
        -fsanitize=undefined -fsanitize=function -fsanitize=cfi -fstack-protector -fstack-protector-strong -flto -fvisibility=hidden
        -D_GLIBCXX_DEBUG=0
        -D_GLIBCXX_ASSERTIONS=0
        -D_LIBCPP_ENABLE_ASSERTIONS=0
        -D_ITERATOR_DEBUG_LEVEL=0
    )
    target_compile_options(utilitiesBOOT PRIVATE
        -fno-exceptions
        -fno-rtti
        -fno-sanitize=all
        -D_GLIBCXX_DEBUG=0
        -D_GLIBCXX_ASSERTIONS=0
        -D_LIBCPP_ENABLE_ASSERTIONS=0
        -D_ITERATOR_DEBUG_LEVEL=0
    )
endif()

