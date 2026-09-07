#pragma once

#include <memory>

class CancellationRegistry;

namespace pdk_chat_demo {

// §4.0.1: shared CancellationRegistry across ChatSession + loop_agent
// Solves C1 (token identity mismatch when ChatSession owns one registry
// and loop_agent owns another). nullptr = "non-cancellable-but-executable"
// fallback for test binaries that don't initialize the global.
extern std::shared_ptr<CancellationRegistry> g_cancellation_registry;

}  // namespace pdk_chat_demo
