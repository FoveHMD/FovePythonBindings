cmake_minimum_required(VERSION 3.10)
project(FovePythonBindings LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set all the output folders to the same directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fove)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fove)

# Create a library target for the SDK
add_library(sdk_lib INTERFACE)
target_include_directories(sdk_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fove_sdk) # Add the include directory

if(WIN32)
	target_link_libraries(sdk_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fove_sdk/FoveClient.lib) # Link against the .lib file
elseif(UNIX AND NOT APPLE)
	target_link_libraries(sdk_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fove_sdk/libFoveClient.so)
endif()
# Add pybind11
add_subdirectory(thirdparty)

pybind11_add_module(FoveClient_Python MODULE
	fove_sdk/FoveAPI.h
	src/bindings.h
	src/bindings.cpp
	src/Fove_CAPI_pybind11.cpp
)

target_link_libraries(FoveClient_Python PRIVATE sdk_lib)

set_target_properties(FoveClient_Python PROPERTIES
	PROJECT_LABEL "Fove Client (Python Bindings)"
	OUTPUT_NAME "capi"
)

# EXAMPLE_VERSION_INFO is defined by setup.py and passed into the C++ code as a
# define (VERSION_INFO) here.
target_compile_definitions(FoveClient_Python
							PRIVATE VERSION_INFO=1.4.0)

add_custom_command(TARGET FoveClient_Python POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
		${CMAKE_SOURCE_DIR}/src/fove/headset.py
		$<TARGET_FILE_DIR:FoveClient_Python>
)



add_custom_command(TARGET FoveClient_Python POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E touch
		$<TARGET_FILE_DIR:FoveClient_Python>/__init__.py
)

if(WIN32)
	add_custom_command(TARGET FoveClient_Python POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
			${CMAKE_SOURCE_DIR}/fove_sdk/FoveClient.dll
			$<TARGET_FILE_DIR:FoveClient_Python>
	)
elseif(UNIX AND NOT APPLE)
	add_custom_command(TARGET FoveClient_Python POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy
		${CMAKE_SOURCE_DIR}/fove_sdk/libFoveClient.so
		$<TARGET_FILE_DIR:FoveClient_Python>
	)
endif()