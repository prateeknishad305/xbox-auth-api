cmake_minimum_required(VERSION 3.20)
project(xbox_live_api_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_library(xboxcore
  src/core/base64.cpp
  src/core/config.cpp
  src/core/cookies.cpp
  src/core/crypto.cpp
  src/core/executor.cpp
  src/core/http.cpp
  src/core/json.cpp
  src/core/log.cpp
  src/core/store.cpp
)
target_include_directories(xboxcore PUBLIC src)
target_link_libraries(xboxcore PUBLIC
  CURL::libcurl
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
  Boost::boost
  Threads::Threads
)
target_compile_options(xboxcore PRIVATE -Wall -Wextra -Wpedantic)

add_library(xboxapi
  src/xbox/auth.cpp
  src/xbox/client.cpp
  src/xbox/endpoints.cpp
  src/xbox/msa.cpp
)
target_link_libraries(xboxapi PUBLIC xboxcore)
target_compile_options(xboxapi PRIVATE -Wall -Wextra -Wpedantic)

add_executable(xbox-api-server src/main.cpp src/http/server.cpp)
target_link_libraries(xbox-api-server PRIVATE xboxapi)
target_compile_options(xbox-api-server PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(xbox-api-tests tests/test_main.cpp)
target_link_libraries(xbox-api-tests PRIVATE xboxapi)
target_compile_options(xbox-api-tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME xbox-api-tests COMMAND xbox-api-tests)
