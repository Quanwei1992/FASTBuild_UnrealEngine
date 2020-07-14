set_property(GLOBAL PROPERTY USE_FOLDERS On)
cmake_minimum_required( VERSION 3.10 )

include(cmake/ucm.cmake)
project(FastBuild)

ucm_set_runtime(STATIC)

add_compile_definitions(__WINDOWS__ WIN64)
add_compile_definitions($<$<CONFIG:Debug>:DEBUG>)
add_compile_definitions($<$<CONFIG:Release>:RELEASE>)


add_subdirectory(External/LZ4)

add_subdirectory(Code/TestFramework)
add_subdirectory(Code/Core)
add_subdirectory(Code/OSUI)
add_subdirectory(Code/Tools/FBuild/FBuildCore)
add_subdirectory(Code/Tools/FBuild/FBuildApp)
add_subdirectory(Code/Tools/FBuild/FBuildWorker)

