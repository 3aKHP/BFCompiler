add_library(bf_common STATIC
    src/lexer.cpp
    src/ir.cpp
    src/parser.cpp
    src/optimizer.cpp
)

target_include_directories(bf_common PUBLIC include)
