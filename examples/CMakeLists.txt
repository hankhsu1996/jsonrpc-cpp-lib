# examples/CMakeLists.txt

# Calculator example using Unix domain socket (pipe) transport
add_executable(pipe_client calculator_example/pipe/client.cpp)
target_link_libraries(pipe_client PRIVATE jsonrpc)

add_executable(pipe_server calculator_example/pipe/server.cpp)
target_link_libraries(pipe_server PRIVATE jsonrpc)

# Calculator example using Framed Unix domain socket transport
add_executable(framed_pipe_client calculator_example/framed_pipe/client.cpp)
target_link_libraries(framed_pipe_client PRIVATE jsonrpc)

add_executable(framed_pipe_server calculator_example/framed_pipe/server.cpp)
target_link_libraries(framed_pipe_server PRIVATE jsonrpc)

# Calculator example using ASIO socket transport
add_executable(socket_client calculator_example/socket/client.cpp)
target_link_libraries(socket_client PRIVATE jsonrpc)

add_executable(socket_server calculator_example/socket/server.cpp)
target_link_libraries(socket_server PRIVATE jsonrpc)

# LSP example using Unix domain socket (pipe) transport
add_executable(lsp_server lsp_example/server/lsp_server.cpp)
target_link_libraries(lsp_server PRIVATE jsonrpc)
