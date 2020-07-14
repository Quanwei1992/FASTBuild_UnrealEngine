set(TARGET_NAME OSUI)

set(SOURCES 
    OSDropDown.cpp
    OSDropDown.h
    OSFont.cpp
    OSFont.h
    OSLabel.cpp
    OSLabel.h
    OSListView.cpp
    OSListView.h
    OSMenu.cpp
    OSMenu.h
    OSSplitter.cpp
    OSSplitter.h
    OSTrayIcon.cpp
    OSTrayIcon.h
    OSWidget.cpp
    OSWidget.h
    OSWindow.cpp
    OSWindow.h
)

add_library(${TARGET_NAME} STATIC ${SOURCES})
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_HOME_DIRECTORY}/Code)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Libs")