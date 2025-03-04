cmake_minimum_required(VERSION 3.27)
project(Marriott)

set(CMAKE_CXX_STANDARD 17)
set(POSTGRESQL_DIR "C:/Program Files/PostgreSQL/16")
link_directories(${POSTGRESQL_DIR}/lib)
include_directories(${POSTGRESQL_DIR}/include)
add_executable(Marriott Connection.cpp
        Hotel_Homepage.cpp
        Connection.h
)
target_link_libraries(Marriott pq)