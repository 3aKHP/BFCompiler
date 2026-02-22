#include "bf/ir.h"

namespace bf {

std::string ir_type_name(IRType type) {
    switch (type) {
        case IRType::MovePtr:   return "MovePtr";
        case IRType::AddVal:    return "AddVal";
        case IRType::Output:    return "Output";
        case IRType::Input:     return "Input";
        case IRType::LoopBegin: return "LoopBegin";
        case IRType::LoopEnd:   return "LoopEnd";
        case IRType::SetZero:   return "SetZero";
    }
    return "Unknown";
}

} // namespace bf
