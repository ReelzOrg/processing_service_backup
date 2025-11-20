cmake_minimum_required(VERSION 3.12)
project(processing_service_backup CXX)

include(CheckIncludeFileCXX)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)

# set(CMAKE_CXX_STANDARD 23)
if (NOT "${CMAKE_CXX_STANDARD}" STREQUAL "")
    # Do nothing
elseif (HAS_ANY AND HAS_STRING_VIEW AND HAS_COROUTINE)
    set(CMAKE_CXX_STANDARD 20)
elseif (HAS_ANY AND HAS_STRING_VIEW)
    set(CMAKE_CXX_STANDARD 17)
else ()
    set(CMAKE_CXX_STANDARD 14)
endif ()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME} main.cc
processing/MediaProcessor.cpp
processing/MediaProcessor.h
./schemas/generated/MediaProcessingJob.hh
)

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# add_subdirectory(drogon) 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
# set(CMAKE_PREFIX_PATH "/usr/local" ${CMAKE_PREFIX_PATH})
find_package(Drogon CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

# set(Avro_DIR "/usr/local/lib/cmake/Avro")
# find_package(Avro REQUIRED)
# target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/schemas/generated)
# target_link_libraries(${PROJECT_NAME} PRIVATE Avro::avrocpp)
# target_link_libraries(${PROJECT_NAME} PRIVATE Avro::avrocpp Avro::avrojson)

# This is manual linking, which is not recommended 
include_directories(/usr/local/include)
link_directories(/usr/local/lib)
target_link_libraries(${PROJECT_NAME} PRIVATE /usr/local/lib/libavrocpp.so)
# target_link_libraries(${PROJECT_NAME} PRIVATE /usr/local/lib/libavrocpp.so /usr/local/lib/libavrojson.so)

find_package(AWSSDK REQUIRED COMPONENTS core s3)
target_link_libraries(${PROJECT_NAME} PRIVATE ${AWSSDK_LINK_LIBRARIES})
# include_directories(/usr/local/include)
# link_directories(/usr/local/lib)

# Manually find cppkafka include and library
find_path(CPPKAFKA_INCLUDE_DIR cppkafka/cppkafka.h PATHS /usr/local/include)
find_library(CPPKAFKA_LIBRARY cppkafka PATHS /usr/local/lib)

# Find librdkafka library
find_library(RDKAFKA_LIBRARY rdkafka PATHS /usr/local/lib)

if (NOT CPPKAFKA_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find cppkafka include directory")
endif()

if (NOT CPPKAFKA_LIBRARY)
    message(FATAL_ERROR "Could not find cppkafka library")
endif()

if (NOT RDKAFKA_LIBRARY)
    message(FATAL_ERROR "Could not find librdkafka library")
endif()

# Include directories and link libraries
target_include_directories(${PROJECT_NAME} PRIVATE ${CPPKAFKA_INCLUDE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/schemas/generated)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CPPKAFKA_LIBRARY} ${RDKAFKA_LIBRARY})

# cppkafka usually does not include cmake pacakage config files, so find_package might not work, hence the above
# find_package(CppKafka REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE CppKafka::cppkafka)

# ##############################################################################

if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "c++17 or higher is required")
elseif (CMAKE_CXX_STANDARD LESS 20)
    message(STATUS "use c++17")
else ()
    message(STATUS "use c++20")
endif ()

aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)
aux_source_directory(models MODEL_SRC)

drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
                    ${CMAKE_CURRENT_BINARY_DIR})
# use the following line to create views with namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE)
# use the following line to create views with namespace CHANGE_ME prefixed
# and path namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE CHANGE_ME)

target_include_directories(${PROJECT_NAME}
                            PRIVATE
                            ${CMAKE_CURRENT_SOURCE_DIR}
                        # ${CMAKE_CURRENT_SOURCE_DIR}/models
                            ${CMAKE_CURRENT_SOURCE_DIR}/utils
                            ${CMAKE_CURRENT_SOURCE_DIR}/processing)
target_sources(${PROJECT_NAME}
               PRIVATE
               main.cc
               processing/MediaProcessor.cpp
               ${CTL_SRC}
               ${FILTER_SRC}
               ${PLUGIN_SRC}
               ${MODEL_SRC})

message(STATUS "Controller sources: ${CTL_SRC}")
message(STATUS "Filter sources: ${FILTER_SRC}")
message(STATUS "Plugin sources: ${PLUGIN_SRC}")
message(STATUS "Model sources: ${MODEL_SRC}")

# ##############################################################################
# uncomment the following line for dynamically loading views 
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

# ##############################################################################

add_subdirectory(test)
