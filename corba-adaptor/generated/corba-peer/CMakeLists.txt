add_library(corba_peer_codec INTERFACE)
target_include_directories(corba_peer_codec INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(corba_peer_codec INTERFACE cpp11::compat)
