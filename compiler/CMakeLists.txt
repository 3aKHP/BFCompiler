add_executable(bf-compiler
    src/main.cpp
    src/codegen.cpp
    src/codegen_masm.cpp
    src/codegen_nasm.cpp
    src/codegen_att.cpp
    src/pe_writer.cpp
)

target_link_libraries(bf-compiler PRIVATE bf_common)
