cmake_minimum_required(VERSION 3.26)


project(gsystemlib)

message(STATUS "gsystem C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "gsystem C++ compiler: ${CMAKE_CXX_COMPILER}")

option(DEBUG "Enable DEBUG output" OFF)

file(GLOB_RECURSE _files "${CMAKE_SOURCE_DIR}/*search.cmake")
list(LENGTH _files _file_exists)
if (${_file_exists} EQUAL 0)
    message(FATAL_ERROR "Unable to find macros search.cmake")
endif()
list(GET _files 0 _file)
include(${_file})
unset(_file)
unset(_files)
unset(_file_exists)


if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_SOURCE_DIR})

    message(STATUS "gsystem generated as test project")

    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
        message(STATUS "Add GCC warnings")
        add_compile_options(-Wall -Wextra -pedantic -Wmissing-include-dirs -Wswitch-default -Wswitch-enum -Wconversion)
    else()
        message(ERROR "Unknown compiler")
    endif()

    option(gsystem_BEDUG "Enable extended warnings output" OFF)

    enable_testing()
    add_subdirectory(test)

    set_target_properties(
        ${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        LINKER_LANGUAGE CXX
    )

else()

    message(STATUS "gsystem added as a library")

    # Поиск библиотек со своим сборщиком
    FIND_CMAKE_LIBS(${PROJECT_NAME}_cmake_dirs "${CMAKE_CURRENT_SOURCE_DIR}/Modules")

    # Указываем исходные файлы проекта
    FILE_PATHS_EXT(c_paths   "${CMAKE_CURRENT_SOURCE_DIR}" "c")
    FILE_PATHS_EXT(cpp_paths "${CMAKE_CURRENT_SOURCE_DIR}" "cpp")
    FILE_PATHS_EXT(s_paths   "${CMAKE_CURRENT_SOURCE_DIR}" "s")
    set(${PROJECT_NAME}_SOURCES "${c_paths};${cpp_paths};${s_paths}")
    EXCLUDE_PATHS(${PROJECT_NAME}_SOURCES "test")
    EXCLUDE_PATHS(${PROJECT_NAME}_SOURCES   "build")
    foreach(cmake_dir ${${PROJECT_NAME}_cmake_dirs})
        EXCLUDE_DIRS(${PROJECT_NAME}_SOURCES ${cmake_dir})
    endforeach()
    add_library(
        ${PROJECT_NAME} 
        STATIC 
        ${${PROJECT_NAME}_SOURCES}
    )

    if (DEBUG)
        message(STATUS "Build gsystem with DEBUG")
        set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG")
    endif()

    # Указываем директории с заголовочными файлами
    FILE_DIRECTORIES_EXT(h_dirs   "${CMAKE_CURRENT_SOURCE_DIR}" "h")
    FILE_DIRECTORIES_EXT(hpp_dirs "${CMAKE_CURRENT_SOURCE_DIR}" "hpp")
    set(${PROJECT_NAME}_INCLUDES "${h_dirs};${hpp_dirs}")
    EXCLUDE_PATHS(${PROJECT_NAME}_INCLUDES   "test")
    EXCLUDE_PATHS(${PROJECT_NAME}_INCLUDES "build")
    foreach(cmake_dir ${${PROJECT_NAME}_cmake_dirs})
        EXCLUDE_DIRS(${PROJECT_NAME}_INCLUDES ${cmake_dir})
    endforeach()

    message(STATUS "${PROJECT_NAME} includes:")
    foreach(dir ${${PROJECT_NAME}_INCLUDES})
        message(STATUS "${dir}")
    endforeach()
    message(STATUS "${PROJECT_NAME} sources:")
    foreach(src ${${PROJECT_NAME}_SOURCES})
        message(STATUS "${src}")
    endforeach()

    target_include_directories(
        ${PROJECT_NAME}
        PUBLIC
        ${${PROJECT_NAME}_INCLUDES}
    )
    target_link_libraries(
        ${PROJECT_NAME}
        storageatlib
        utilslib
    )
    target_link_libraries(
        ${CMAKE_PROJECT_NAME}
        ${PROJECT_NAME}
    )

endif()