add_library(tcp_peer_codec INTERFACE)
target_include_directories(tcp_peer_codec INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tcp_peer_codec INTERFACE cpp11::compat)
