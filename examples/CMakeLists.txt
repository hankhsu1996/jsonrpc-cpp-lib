# examples/CMakeLists.txt

# Calculator example using Unix domain socket (pipe) transport
add_executable(pipe_client calculator_example/pipe/client.cpp)
target_link_libraries(pipe_client PRIVATE jsonrpc-cpp-lib)

add_executable(pipe_server calculator_example/pipe/server.cpp)
target_link_libraries(pipe_server PRIVATE jsonrpc-cpp-lib)

# Calculator example using Framed Unix domain socket transport
add_executable(framed_pipe_client calculator_example/framed_pipe/client.cpp)
target_link_libraries(framed_pipe_client PRIVATE jsonrpc-cpp-lib)

add_executable(framed_pipe_server calculator_example/framed_pipe/server.cpp)
target_link_libraries(framed_pipe_server PRIVATE jsonrpc-cpp-lib)

# Calculator example using ASIO socket transport
add_executable(socket_client calculator_example/socket/client.cpp)
target_link_libraries(socket_client PRIVATE jsonrpc-cpp-lib)

add_executable(socket_server calculator_example/socket/server.cpp)
target_link_libraries(socket_server PRIVATE jsonrpc-cpp-lib)

# Calculator example using Framed ASIO socket transport
add_executable(framed_socket_client calculator_example/framed_socket/client.cpp)
target_link_libraries(framed_socket_client PRIVATE jsonrpc-cpp-lib)

add_executable(framed_socket_server calculator_example/framed_socket/server.cpp)
target_link_libraries(framed_socket_server PRIVATE jsonrpc-cpp-lib)

# LSP example using Unix domain socket (pipe) transport
add_executable(pipe_lsp_server lsp_example/pipe/lsp_server.cpp)
target_link_libraries(pipe_lsp_server PRIVATE jsonrpc-cpp-lib)

# LSP example using ASIO socket transport
add_executable(socket_lsp_server lsp_example/socket/lsp_server.cpp)
target_link_libraries(socket_lsp_server PRIVATE jsonrpc-cpp-lib)
