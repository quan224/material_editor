#pragma once
#include "Core/Public/UUID.h"
#include <string>

// 错误级别
enum class EErrorSeverity {
    Error,
    Warning,
    Info
};

struct CompilerError {

    UUID            node_id = UUID::Invalid();
    std::string     pin_name;
    std::string     error_message;
    EErrorSeverity  severity = EErrorSeverity::Error;

    bool SameAs(const CompilerError& other) const {
        return node_id == other.node_id
            && pin_name == other.pin_name
            && error_message == other.error_message;
    }
};