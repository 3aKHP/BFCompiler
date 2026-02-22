#include "bf/lexer.h"
#include <algorithm>

namespace bf {

std::vector<char> lex(const std::string& source) {
    std::vector<char> tokens;
    tokens.reserve(source.size());
    for (char c : source) {
        if (c == '>' || c == '<' || c == '+' || c == '-' ||
            c == '.' || c == ',' || c == '[' || c == ']') {
            tokens.push_back(c);
        }
    }
    return tokens;
}

} // namespace bf
