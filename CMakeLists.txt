cmake_minimum_required(VERSION 3.16.3)

project(poca-websocket-cpp)

IF (CMAKE_BUILD_TYPE STREQUAL Debug)       
    ADD_DEFINITIONS(-DDEBUG)                            
ENDIF()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/util)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/util POCA_WEBSOCKET_CPP_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src POCA_WEBSOCKET_CPP_SRC)

add_executable(poca-ws-client ${CMAKE_CURRENT_SOURCE_DIR}/client.cpp ${POCA_WEBSOCKET_CPP_SRC})
target_link_libraries(poca-ws-client ${VIDEO_PROCESSER_LIB_NAME} websockets pthread)

add_executable(poca-ws-server ${CMAKE_CURRENT_SOURCE_DIR}/server.cpp ${POCA_WEBSOCKET_CPP_SRC})
target_link_libraries(poca-ws-server ${VIDEO_PROCESSER_LIB_NAME} websockets pthread)