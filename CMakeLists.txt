cmake_minimum_required(VERSION 3.20)
project(intercom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.3
  GIT_SHALLOW TRUE
)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

add_executable(intercom
  src/main.cpp
  src/config.cpp
  src/stt_whisper.cpp
  src/tts_kokoro.cpp
  src/arbiter_client.cpp
  src/filler_client.cpp
  src/session_store.cpp
  src/fast_path.cpp
  src/turn_pipeline.cpp
  src/http_server.cpp
  src/util.cpp
  src/speakable.cpp
  src/managed_server.cpp
)

target_include_directories(intercom PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(intercom PRIVATE
  nlohmann_json::nlohmann_json
  httplib::httplib
  SQLite3::SQLite3
  Threads::Threads
)

target_compile_definitions(intercom PRIVATE
  "INTERCOM_SCRIPTS_DIR=\"${CMAKE_SOURCE_DIR}/scripts\""
)
if(APPLE)
  target_compile_definitions(intercom PRIVATE CPPHTTPLIB_USE_POLL)
endif()

install(TARGETS intercom RUNTIME DESTINATION bin)
install(FILES config/intercom.example.json config/arthur.agent.json DESTINATION share/intercom)
install(FILES scripts/kokoro_server.py DESTINATION share/intercom)

enable_testing()

add_executable(test_flush_sentences
  tests/test_flush_sentences.cpp
  src/util.cpp
)
target_include_directories(test_flush_sentences PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_test(NAME test_flush_sentences COMMAND test_flush_sentences)

add_executable(test_wav_util
  tests/test_wav_util.cpp
  src/util.cpp
)
target_include_directories(test_wav_util PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_test(NAME test_wav_util COMMAND test_wav_util)

add_executable(test_sentence_tts
  tests/test_sentence_tts.cpp
  src/turn_pipeline.cpp
  src/speakable.cpp
  src/util.cpp
  src/session_store.cpp
  src/fast_path.cpp
  src/arbiter_client.cpp
  src/filler_client.cpp
)
target_include_directories(test_sentence_tts PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(test_sentence_tts PRIVATE
  nlohmann_json::nlohmann_json
  httplib::httplib
  SQLite3::SQLite3
  Threads::Threads
)
if(APPLE)
  target_compile_definitions(test_sentence_tts PRIVATE CPPHTTPLIB_USE_POLL)
endif()
add_test(NAME test_sentence_tts COMMAND test_sentence_tts)

