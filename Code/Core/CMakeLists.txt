set(TARGET_NAME Core)

ucm_add_dirs(Containers TO SOURCES RECURSIVE)
ucm_add_dirs(CoreTest TO SOURCES RECURSIVE)
ucm_add_dirs(Env TO SOURCES RECURSIVE)
ucm_add_dirs(FileIO TO SOURCES RECURSIVE)
ucm_add_dirs(Math TO SOURCES RECURSIVE)
ucm_add_dirs(Mem TO SOURCES RECURSIVE)
ucm_add_dirs(Network TO SOURCES RECURSIVE)
ucm_add_dirs(Process TO SOURCES RECURSIVE)
ucm_add_dirs(Profile TO SOURCES RECURSIVE)
ucm_add_dirs(Reflection TO SOURCES RECURSIVE)
ucm_add_dirs(Strings TO SOURCES RECURSIVE)
ucm_add_dirs(Time TO SOURCES RECURSIVE)
ucm_add_dirs(Tracing TO SOURCES RECURSIVE)
add_library(Core STATIC ${SOURCES} Core.natvis)

target_link_libraries(${TARGET_NAME} PUBLIC TestFramework lz4)


set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Libs")
