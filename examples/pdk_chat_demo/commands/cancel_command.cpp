#include "commands/cancel_command.h"

#include "commands/command_globals.h"
#include "chat_session.h"

namespace pdk_chat_demo {

hydraforge::pdk::CommandSpec make_cancel_command_spec() {
    hydraforge::pdk::CommandSpec spec;
    spec.name = "/cancel";
    spec.description = "Cancel the in-flight turn (no-op if no active turn)";
    spec.handler = [](agenticdsl::ToolCallContext&) -> std::string {
        if (pdk_chat_demo::g_command_session == nullptr) {
            return "[cancel] no active session";
        }
        // §7.7: request_stop() is safe — early-returns if no active turn
        pdk_chat_demo::g_command_session->request_stop();
        return "[cancel] requested";
    };
    return spec;
}

}  // namespace pdk_chat_demo