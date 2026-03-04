cmake_minimum_required(VERSION 3.24)
project(blackwire_client_cpp_gui VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network WebSockets Multimedia MultimediaWidgets)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

set(BW_HEADERS
  include/blackwire/interfaces/api_client.hpp
  include/blackwire/interfaces/crypto_service.hpp
  include/blackwire/interfaces/secret_store.hpp
  include/blackwire/interfaces/ws_client.hpp
  include/blackwire/interfaces/audio_call_engine.hpp
  include/blackwire/models/dto.hpp
  include/blackwire/models/view_models.hpp
  include/blackwire/storage/client_state.hpp
  include/blackwire/storage/state_store.hpp
  include/blackwire/storage/windows_credential_store.hpp
  include/blackwire/storage/in_memory_secret_store.hpp
  include/blackwire/api/qt_api_client.hpp
  include/blackwire/crypto/sodium_crypto_service.hpp
  include/blackwire/ws/qt_ws_client.hpp
  include/blackwire/audio/qt_audio_call_engine.hpp
  include/blackwire/controller/application_controller.hpp
  include/blackwire/ui/login_widget.hpp
  include/blackwire/ui/device_setup_widget.hpp
  include/blackwire/ui/chat_widget.hpp
  include/blackwire/ui/main_window.hpp
  include/blackwire/ui/settings_dialog.hpp
  include/blackwire/ui/theme.hpp
  include/blackwire/util/auth_retry.hpp
  include/blackwire/util/conversation_view.hpp
  include/blackwire/util/diagnostics.hpp
  include/blackwire/util/message_view.hpp
  include/blackwire/util/peer_address.hpp
  include/blackwire/smoke/smoke_runner.hpp
)

set(BW_SOURCES
  src/main.cpp
  src/storage/state_store.cpp
  src/storage/windows_credential_store.cpp
  src/storage/in_memory_secret_store.cpp
  src/api/qt_api_client.cpp
  src/crypto/sodium_crypto_service.cpp
  src/ws/qt_ws_client.cpp
  src/audio/qt_audio_call_engine.cpp
  src/controller/application_controller.cpp
  src/ui/login_widget.cpp
  src/ui/device_setup_widget.cpp
  src/ui/chat_widget.cpp
  src/ui/main_window.cpp
  src/ui/settings_dialog.cpp
  src/ui/theme.cpp
  src/util/conversation_view.cpp
  src/util/diagnostics.cpp
  src/util/message_view.cpp
  src/util/peer_address.cpp
  src/smoke/smoke_runner.cpp
)

add_executable(blackwire_client ${BW_HEADERS} ${BW_SOURCES})
target_include_directories(blackwire_client PRIVATE include)

target_link_libraries(blackwire_client PRIVATE
  Qt6::Core
  Qt6::Widgets
  Qt6::Network
  Qt6::WebSockets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  nlohmann_json::nlohmann_json
  unofficial-sodium::sodium
  spdlog::spdlog
)

if (WIN32)
  target_link_libraries(blackwire_client PRIVATE Advapi32)
endif()

target_compile_definitions(blackwire_client PRIVATE BLACKWIRE_CLIENT_VERSION=\"${PROJECT_VERSION}\")

include(CTest)
if (BUILD_TESTING)
  add_executable(blackwire_client_tests
    tests/test_crypto.cpp
    tests/test_envelope_serialization.cpp
    tests/test_secret_store.cpp
    tests/test_state_dedup.cpp
    tests/test_auth_retry.cpp
    tests/test_peer_address.cpp
    tests/test_conversation_view.cpp
    tests/test_diagnostics.cpp
    tests/test_message_view.cpp
    tests/test_call_dto.cpp
    tests/test_state_store_compat.cpp
    src/storage/in_memory_secret_store.cpp
    src/crypto/sodium_crypto_service.cpp
    src/util/conversation_view.cpp
    src/util/diagnostics.cpp
    src/util/message_view.cpp
    src/util/peer_address.cpp
  )

  target_include_directories(blackwire_client_tests PRIVATE include)

  target_link_libraries(blackwire_client_tests PRIVATE
    Qt6::Core
    nlohmann_json::nlohmann_json
    unofficial-sodium::sodium
    GTest::gtest
    GTest::gtest_main
  )

  include(GoogleTest)
  gtest_discover_tests(blackwire_client_tests)
endif()
