set(TARGET_NAME lz4)


add_library(${TARGET_NAME} STATIC 
    lz4.c
    lz4frame.c
    lz4hc.c
    xxhash.c)

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "External")