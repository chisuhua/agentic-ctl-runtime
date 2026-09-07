#pragma once

#include <common/tools/command_registry.h>

namespace pdk_chat_demo {

// §7.7 chat-async-io-consumer-loop: /cancel command
// Handler calls g_command_session->request_stop() — no-op if no active turn
hydraforge::pdk::CommandSpec make_cancel_command_spec();

}  // namespace pdk_chat_demo