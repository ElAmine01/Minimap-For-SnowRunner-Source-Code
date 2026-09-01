#pragma once
/// Locates the two engine globals the mod depends on by looking at what they
/// point to, instead of hardcoding module-relative addresses that die on every
/// game patch. See MAINTENANCE.md for the discovery strategy.

#include <cstdint>

#include "MemoryScan.h"

namespace snowmap::game {

/// Byte offsets inside the Husky structures the mod walks. These have been
/// stable across seasons; only the addresses of the globals move on a patch.
namespace layout {
inline constexpr uintptr_t kSessionNameLength = 0x10;
inline constexpr uintptr_t kSessionName       = 0x18;
inline constexpr uintptr_t kTruckSimNode      = 0x08;
inline constexpr uintptr_t kSimNodeChild      = 0x60;
inline constexpr uintptr_t kChildChassisBody  = 0x68;
inline constexpr uintptr_t kBodyRightVector   = 0xB0;
inline constexpr uintptr_t kBodyWorldPosition = 0xC0;

/// Longest level id that still fits the session object's inline buffer.
inline constexpr uint32_t  kMaxInlineNameLength = 31;
} // namespace layout

/// Shape test for a candidate chassis body: a unit-length right vector and a
/// finite world position inside the playable bounds. Shared with the entity
/// tracker, which uses it to reject heap objects that merely share a vtable
/// slot value.
bool ChassisBodyLooksValid(const scan::RegionMap& regions, uintptr_t body);

/// Same test against a live object, without a region snapshot.
bool ChassisBodyLooksValid(uintptr_t body);

/// Mangled RTTI class name of the TRUCK_CONTROL global, used as the anchor for
/// entity discovery.
const char* TruckControlClassName();

/// Module base once the scanner worker has parsed the headers, else zero.
uintptr_t ModuleBase();

/// Module layout snapshot; `Valid()` is false until the worker has started.
const scan::ModuleLayout& MainModule();

/// Module-relative addresses of the globals, or zero while still unknown.
struct GameOffsets
{
    uintptr_t session_rva       = 0;
    uintptr_t truck_control_rva = 0;

    bool Complete() const { return session_rva != 0 && truck_control_rva != 0; }
};

/// Background discovery of the engine globals, with an on-disk cache so the
/// scan only runs the first time a given game build is seen.
class OffsetScanner
{
public:
    /// Launch the discovery worker. Safe to call more than once.
    static void Start();
    /// Signal the worker to stop. Does not block.
    static void Shutdown();

    /// Latest known offsets; fields stay zero until discovery resolves them.
    static GameOffsets Current();

    /// Absolute address of a global, or zero when it is not known yet.
    static uintptr_t SessionGlobal();
    static uintptr_t TruckControlGlobal();

    /// Feedback from the pollers. A global that stops resolving for long enough
    /// is treated as stale and triggers a rescan.
    static void ReportSessionResult(bool ok);
    static void ReportTruckResult(bool ok, bool level_active);
};

} // namespace snowmap::game
