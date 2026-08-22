#pragma once

#include "exchange/core/types.hpp"
#include <string>
#include <unordered_map>

namespace exchange::gateway {

// Simple bidirectional symbol <-> InstrumentId table. Populated at
// startup from config (see config/instruments.txt) or programmatically
// in tests.
class SymbolTable {
public:
    core::InstrumentId intern(const std::string& symbol) {
        auto it = symbol_to_id_.find(symbol);
        if (it != symbol_to_id_.end()) return it->second;
        core::InstrumentId id = core::make_instrument_id(next_id_++);
        symbol_to_id_.emplace(symbol, id);
        id_to_symbol_.emplace(id, symbol);
        return id;
    }

    [[nodiscard]] core::InstrumentId resolve(const std::string& symbol) const {
        auto it = symbol_to_id_.find(symbol);
        return it == symbol_to_id_.end() ? core::make_instrument_id(0) : it->second;
    }

    [[nodiscard]] const std::string& symbol_of(core::InstrumentId id) const {
        static const std::string kUnknown = "UNKNOWN";
        auto it = id_to_symbol_.find(id);
        return it == id_to_symbol_.end() ? kUnknown : it->second;
    }

private:
    std::unordered_map<std::string, core::InstrumentId> symbol_to_id_;
    std::unordered_map<core::InstrumentId, std::string> id_to_symbol_;
    std::uint32_t next_id_ = 1;
};

} // namespace exchange::gateway
