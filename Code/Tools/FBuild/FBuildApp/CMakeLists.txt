set(TARGET_NAME FBuild)


add_executable(${TARGET_NAME} Main.cpp)
target_link_libraries(${TARGET_NAME} PRIVATE Core FBuildCore Advapi32.lib kernel32.lib Ws2_32.lib User32.lib)


set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Apps")

install(TARGETS ${TARGET_NAME} DESTINATION   ${CMAKE_HOME_DIRECTORY}/bin)