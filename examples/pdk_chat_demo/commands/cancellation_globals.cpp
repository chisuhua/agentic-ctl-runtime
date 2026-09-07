#include "commands/cancellation_globals.h"

#include "cancellation_registry.h"

namespace pdk_chat_demo {

// §4.0.1: defaults to nullptr; main.cpp sets it before constructing ChatSession
std::shared_ptr<CancellationRegistry> g_cancellation_registry = nullptr;

}  // namespace pdk_chat_demo
