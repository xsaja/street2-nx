// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
//
// (2026-10-26: a verbose-only hook on sub_002DAA60, the game's pool free-size walk, found the Switch's oversized
// NtReadFile; see CLAUDE.md and the git history for it.)
// FIFA Street 1 needed a compare-and-swap on EA's sound engine guard (its sub_0022004B; see street-nx). FIFA Street 2's
// engine guards itself with a real semaphore (0x23044C enter, 0x230477 leave, object at [0x350C74]), so that race is gone.
#include <atomic>

#include "core/log.h"
#include "kernel/kernel.h"

// EA sound engine: a channel (table at [0x350C28]+0x40..) keeps its voice object at +0x12C, made by 0x2302F2 through the
// voice factory at [0x350C70]. When the factory gives no voice the channel stays registered without one, and most of
// the engine calls through +0x12C without a check (the Xbox's factory never failed). Seen in Ryujinx at 23 s.

X86_EXTERN_FUNC(__imp__sub_002302F2);
// channel's voice creation: logs when it ends without a voice (returns the voice, 0 if none)
X86_FUNC(sub_002302F2) {
    const uint32_t channel_id = kernel::R32(ctx.esp + 4);
    __imp__sub_002302F2(ctx, base);
    static std::atomic<int> logged{0};
    if (!ctx.eax && logged.fetch_add(1, std::memory_order_relaxed) < 16)
        LOGW("sound: channel %08X ended without a voice (busy engine, unknown channel or the factory failed)", channel_id);
}

X86_EXTERN_FUNC(__imp__sub_0022FD8C);
// stops one channel (from the stop-all walk at 0x22FDF3): calls voice->[0xC], then marks the channel stopped. A channel
// without a voice has nothing to stop: mark it stopped as the original does and skip the null call.
X86_FUNC(sub_0022FD8C) {
    const uint32_t channel = kernel::R32(ctx.esp + 4);
    if (kernel::R32(channel + 0x12C)) {
        __imp__sub_0022FD8C(ctx, base);
        return;
    }
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1, std::memory_order_relaxed) < 16)
        LOGW("sound: stop of channel %08X without a voice skipped", channel);
    kernel::W16(channel + 0x4, 0xFFFF);
    kernel::W16(channel + 0xA, kernel::R16(channel + 0xA) | 0x1F0);
    kernel::Return(ctx, 0, 0);
}
