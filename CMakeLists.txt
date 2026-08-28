cmake_minimum_required(VERSION 3.21)
set(PROJECT_NAME numen)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
project(${PROJECT_NAME} VERSION 0.4.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_REPL "Build the simple repl program" OFF)
option(BUILD_REPL_CURRENCY_PROVIDER "Build the built-in REPL currency provider (uses api.vicinae.com). Adds a runtime dependency on OpenSSL to run the REPL." ON)

option(BUILD_TESTS "Build the test suite" OFF)

# Apple's libc++ has no std::chrono tzdb: see upstream PR https://github.com/llvm/llvm-project/pull/122010
option(NUMEN_USE_DATE_TZ "Use the vendored HowardHinnant/date library for timezone support instead of std::chrono" ${APPLE})

option(BUILD_FUZZER "Build the libFuzzer target. Requires clang." OFF)

option(NUMEN_HARDENING "Enable hardening compiler and linker flags" ON)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

if (BUILD_FUZZER)
	if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		message(FATAL_ERROR "BUILD_FUZZER requires clang (got '${CMAKE_CXX_COMPILER_ID}'). Configure with -DCMAKE_CXX_COMPILER=clang++.")
	endif()

	# global: coverage feedback needs the library instrumented, not just the entry point
	add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address,undefined)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND ${PROJECT_IS_TOP_LEVEL})
	if (MSVC)
		add_compile_options(/fsanitize=address)
	else()
		add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
		add_link_options(-fsanitize=address,undefined)
	endif()
endif()

include(GNUInstallDirs)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

if (MSVC)
	add_compile_options(/permissive- /Zc:preprocessor /Zc:__cplusplus /bigobj /utf-8 /wd4996)
	add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

	set(NUMEN_WARNING_OPTIONS /W4)
	if (NUMEN_HARDENING)
		set(NUMEN_HARDENING_OPTIONS /sdl /guard:cf)
		set(NUMEN_HARDENING_LINK_OPTIONS /guard:cf)
	endif()
else()
	set(NUMEN_WARNING_OPTIONS
		-Wall -Wextra -Wpedantic
		-Wshadow
		-Wnon-virtual-dtor
		-Woverloaded-virtual
		-Wcast-align
		-Wdouble-promotion
		-Wimplicit-fallthrough
		-Wformat=2
		-Wundef
		-Wextra-semi
		-Wconversion
		-Wsign-conversion
		-Wno-missing-field-initializers
	)
	if (NUMEN_HARDENING)
		set(NUMEN_HARDENING_OPTIONS
			-fstack-protector-strong
			-D_GLIBCXX_ASSERTIONS
			# needs optimization; sanitized builds (Debug, fuzzer) conflict with it
			$<$<AND:$<NOT:$<CONFIG:Debug>>,$<NOT:$<BOOL:${BUILD_FUZZER}>>>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>
		)
		if (NOT APPLE)
			list(APPEND NUMEN_HARDENING_OPTIONS -fstack-clash-protection)
			if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
				list(APPEND NUMEN_HARDENING_OPTIONS -fcf-protection=full)
			elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
				list(APPEND NUMEN_HARDENING_OPTIONS -mbranch-protection=standard)
			endif()
			set(NUMEN_HARDENING_LINK_OPTIONS
				LINKER:-z,relro LINKER:-z,now LINKER:-z,noexecstack)
		endif()
	endif()
endif()

function(numen_harden target)
	target_compile_options(${target} PRIVATE ${NUMEN_WARNING_OPTIONS} ${NUMEN_HARDENING_OPTIONS})
	target_link_options(${target} PRIVATE ${NUMEN_HARDENING_LINK_OPTIONS})
endfunction()

if (WIN32)
	set(DEFAULT_USE_SYSTEM_CATCH OFF)
else()
	set(DEFAULT_USE_SYSTEM_CATCH ON)
endif()

option(USE_SYSTEM_CATCH "Use system catch2 for testing. Fetch it from GitHub otherwise." ${DEFAULT_USE_SYSTEM_CATCH})

if (WIN32 AND NOT MSVC)
	message(FATAL_ERROR "Windows builds are only supported with MSVC (got '${CMAKE_CXX_COMPILER_ID}'). Please set -DCMAKE_CXX_COMPILER=cl.")
endif()

set(NUMEN_SRCS 
	src/numen/numen.cpp
	src/numen/ast-printer.cpp
	src/numen/builtin-units.cpp
	src/numen/datetime.cpp
	src/numen/duration.cpp
	src/numen/fn.cpp
	src/numen/interpreter.cpp
	src/numen/lexer.cpp
	src/numen/parser.cpp
	src/numen/region-currency.cpp
	src/numen/timezone.cpp
	src/numen/unicode.cpp
	src/numen/unit.cpp
	src/numen/value.cpp
)


if (BUILD_SHARED_LIBS)
	add_library(${PROJECT_NAME} SHARED ${NUMEN_SRCS})
else()
	add_library(${PROJECT_NAME} STATIC ${NUMEN_SRCS})
endif()

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
numen_harden(${PROJECT_NAME})

if (NUMEN_USE_DATE_TZ)
	set(DATE_DIR ${VENDOR_DIR}/date)
	target_sources(${PROJECT_NAME} PRIVATE ${DATE_DIR}/src/tz.cpp)
	# don't check vendored
	set_source_files_properties(${DATE_DIR}/src/tz.cpp PROPERTIES COMPILE_OPTIONS "-w")
	# public: <numen/tz.hpp> and date's class layouts depend on these
	target_compile_definitions(${PROJECT_NAME} PUBLIC NUMEN_USE_DATE_TZ=1 USE_OS_TZDB=1 HAS_REMOTE_API=0)
	target_include_directories(${PROJECT_NAME} PUBLIC
		$<BUILD_INTERFACE:${DATE_DIR}/include>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	)
	install(DIRECTORY ${DATE_DIR}/include/date DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

	if (MSVC)
		message(FATAL_ERROR "NUMEN_USE_DATE_TZ is not supported with MSVC. Use std::chrono instead.")
	endif()
endif()

# no ABI stability for now: every release is its own soname
set_target_properties(${PROJECT_NAME} PROPERTIES
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION}
)

target_include_directories(${PROJECT_NAME} INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

include(CMakePackageConfigHelpers)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/numen/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/numen)

set(NUMEN_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
install(EXPORT ${PROJECT_NAME}Targets NAMESPACE ${PROJECT_NAME}:: DESTINATION ${NUMEN_CMAKE_DIR})
configure_package_config_file(cmake/numenConfig.cmake.in
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
	INSTALL_DESTINATION ${NUMEN_CMAKE_DIR}
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake COMPATIBILITY ExactVersion)
install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
	DESTINATION ${NUMEN_CMAKE_DIR}
)
target_include_directories(${PROJECT_NAME} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${VENDOR_DIR})

if (BUILD_REPL)
	set(REPL_TARGET repl)
	set(SRCS src/repl/main.cpp)
	set(LINK_LIBS)

	if (BUILD_REPL_CURRENCY_PROVIDER)
		find_package(OpenSSL REQUIRED)
		add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT=1)
		add_compile_definitions(BUILD_CURRENCY_PROVIDER=1)
		list(APPEND SRCS
			src/repl/vicinae-currency-provider.cpp
		)
		list(APPEND LINK_LIBS
			OpenSSL::SSL OpenSSL::Crypto
		)
	endif()

	add_executable(${REPL_TARGET} ${SRCS})
	numen_harden(${REPL_TARGET})
	target_include_directories(${REPL_TARGET} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src
	)
	target_include_directories(${REPL_TARGET} SYSTEM PRIVATE ${VENDOR_DIR})
	target_link_libraries(${REPL_TARGET} PRIVATE ${PROJECT_NAME} ${LINK_LIBS})
	target_include_directories(${REPL_TARGET} PRIVATE ./include ./repl)
	set_target_properties(${REPL_TARGET} PROPERTIES OUTPUT_NAME numen)
	install(TARGETS ${REPL_TARGET})
endif()

if (BUILD_TESTS)
	if (USE_SYSTEM_CATCH)
		find_package(Catch2 3 REQUIRED)
	else()
		Include(FetchContent)

		FetchContent_Declare(
		  Catch2
		  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
		  GIT_TAG        v3.8.1
		)
		FetchContent_MakeAvailable(Catch2)
		list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
	endif()

	set(TEST_TARGET numen-tests)
	add_executable(${TEST_TARGET} 
		tests/main.cpp 
		tests/datetime.cpp 
		tests/timezone.cpp 
		tests/parse.cpp
		tests/unit.cpp 
		tests/corpus.cpp 
		tests/currency.cpp 
		tests/conversion.cpp
		tests/value.cpp
		tests/strict.cpp
		tests/locale.cpp
	)
	target_compile_definitions(${TEST_TARGET} PRIVATE CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus")

	numen_harden(${TEST_TARGET})
	target_link_libraries(${TEST_TARGET} PRIVATE Catch2::Catch2WithMain ${PROJECT_NAME})
	target_include_directories(${TEST_TARGET} PRIVATE ./include ./src/numen)

	include(CTest)
	include(Catch)
	catch_discover_tests(${TEST_TARGET})
endif()

if (BUILD_FUZZER)
	set(FUZZ_TARGET numen-fuzz)
	add_executable(${FUZZ_TARGET} tests/fuzz.cpp)
	numen_harden(${FUZZ_TARGET})
	target_link_libraries(${FUZZ_TARGET} PRIVATE ${PROJECT_NAME})
	target_include_directories(${FUZZ_TARGET} PRIVATE ./include)

	target_link_options(${FUZZ_TARGET} PRIVATE -fsanitize=fuzzer)
endif()
