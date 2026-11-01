set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES TOOLCHAIN_BIN_DIR)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
if(NOT TOOLCHAIN_BIN_DIR)
    if(DEFINED ENV{ARM_GCC_BIN} AND NOT "$ENV{ARM_GCC_BIN}" STREQUAL "")
        set(TOOLCHAIN_BIN_DIR "$ENV{ARM_GCC_BIN}" CACHE PATH "Arm GNU toolchain bin directory")
    else()
        find_program(ARM_GCC_EXECUTABLE NAMES arm-none-eabi-gcc REQUIRED)
        get_filename_component(TOOLCHAIN_BIN_DIR "${ARM_GCC_EXECUTABLE}" DIRECTORY)
        set(TOOLCHAIN_BIN_DIR "${TOOLCHAIN_BIN_DIR}" CACHE PATH "Arm GNU toolchain bin directory")
    endif()
endif()
if(CMAKE_HOST_WIN32)
    set(TOOLCHAIN_EXE_SUFFIX ".exe")
else()
    set(TOOLCHAIN_EXE_SUFFIX "")
endif()
set(TOOLCHAIN_PREFIX                "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-")

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_EXE_SUFFIX})
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++${TOOLCHAIN_EXE_SUFFIX})
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++${TOOLCHAIN_EXE_SUFFIX})
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy${TOOLCHAIN_EXE_SUFFIX})
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size${TOOLCHAIN_EXE_SUFFIX})

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")

# The cyclomatic-complexity parameter must be defined for the Cyclomatic complexity feature in STM32CubeIDE to work.
# However, most GCC toolchains do not support this option, which causes a compilation error; for this reason, the feature is disabled by default.
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcyclomatic-complexity")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-Os -g0")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-Os -g0")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32F405xx_FLASH.ld\"")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --specs=nano.specs")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(TOOLCHAIN_LINK_LIBRARIES "m")


