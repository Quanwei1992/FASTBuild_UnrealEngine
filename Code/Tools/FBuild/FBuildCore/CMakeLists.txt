set(TARGET_NAME FBuildCore)

ucm_add_dirs(
    BFF
    Cache
    Graph
    Helpers
    Protocol
    WorkerPool
    TO SOURCES RECURSIVE)

ucm_add_files(
    "Error.cpp" 
"Error.h" 
"FBuild.cpp"
"FBuild.h"
"FBuildOptions.h"
"FBuildOptions.cpp"
"FBuildVersion.h"
"FLog.h"
"FLog.cpp"
TO SOURCES)

add_library(${TARGET_NAME} STATIC ${SOURCES})
target_link_libraries(${TARGET_NAME} PUBLIC lz4)
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_HOME_DIRECTORY}/Code)

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Libs")