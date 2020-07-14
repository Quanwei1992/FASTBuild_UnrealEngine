set(TARGET_NAME TestFramework)
add_library(TestFramework STATIC
    UnitTest.cpp
    UnitTestManager.cpp
    UnitTest.h
    UnitTestManager.h
)

target_include_directories(${TARGET_NAME} PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_HOME_DIRECTORY}/Code)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Test")