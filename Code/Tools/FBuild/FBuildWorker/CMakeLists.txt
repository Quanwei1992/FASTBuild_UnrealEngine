set(TARGET_NAME FBuildWorker)

ucm_add_dirs(
    Worker
    TO SOURCES RECURSIVE)


set(RESOURCE_FILES
    MainWindowResource.rc
)


ucm_add_files(
    "FBuildWorkerOptions.cpp" 
    "FBuildWorkerOptions.h"
    "Main.cpp"
    "resource.h"
    TO SOURCES)


add_executable(${TARGET_NAME} ${SOURCES} ${RESOURCE_FILES})

target_link_libraries(${TARGET_NAME} PRIVATE Core FBuildCore lz4 OSUI
    Advapi32.lib
    kernel32.lib
    Ws2_32.lib
    User32.lib
    Shell32.lib
    Comctl32.lib
    Gdi32.lib
    psapi.lib
)


set_target_properties(${TARGET_NAME} PROPERTIES
    RESOURCE ${RESOURCE_FILES}
  )


set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Apps")
target_link_options(${TARGET_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
install(TARGETS ${TARGET_NAME} DESTINATION  ${CMAKE_HOME_DIRECTORY}/bin)