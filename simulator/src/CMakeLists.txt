cmake_minimum_required(VERSION 3.12)

project(
  migsim
  LANGUAGES CXX
)

file(GLOB SOURCES main.cpp analysis.cpp)

# find_package(libconfig REQUIRED)  # Ubuntu libconfig-dev ships only pkg-config; link via -lconfig below

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -O0 -fsanitize=address -static-libasan")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -g -O3 -march=native -flto")

add_subdirectory(pol)
add_subdirectory(opt)


set(LIBRARIES pol opt)

foreach(LIBRARY ${LIBRARIES})
  set(LIB_OBJS ${LIB_OBJS} $<TARGET_OBJECTS:${LIBRARY}>)
endforeach()



#add_library(${PROJECT_NAME} STATIC ${LIB_OBJS})
add_executable(${PROJECT_NAME} ${SOURCES} ${LIB_OBJS})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME} config)
