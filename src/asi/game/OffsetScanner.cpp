#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "OffsetScanner.h"

#include "../core/Logger.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace snowmap::game {
namespace {

/// MSVC mangled name of the class the active truck controller instantiates.
constexpr char     kTruckControlClass[] = ".?AVTRUCK_CONTROL@combine@@";
constexpr char     kLevelPrefix[]       = "level_";
constexpr size_t   kLevelPrefixLength   = sizeof(kLevelPrefix) - 1;
constexpr uintptr_t kMinUserAddress     = 0x10000;
constexpr uintptr_t kPageSize           = 0x1000;

/// Upper bound of the user address space. Must be queried, not guessed: the
/// game module itself loads around 0x7FF7'xxxx'xxxx, above any round constant
/// one is tempted to hardcode here.
uintptr_t MaxUserAddress()
{
    static const uintptr_t cached = [] {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    }();
    return cached;
}

/// Rescan thresholds. The session global resolves even in the main menu, so it
/// failing at all is a strong signal the module layout moved. The truck chain
/// is legitimately null whenever no truck is spawned, so it gets a long grace.
constexpr uint64_t kSessionGraceMs = 3000;
constexpr uint64_t kTruckGraceMs   = 60000;
constexpr DWORD    kRetryIntervalMs = 2000;
/// One "still searching" line every 30 s while a global stays unresolved.
constexpr uint32_t kHeartbeatPasses = 15;

#if defined(_MSC_VER)
/// A region can be freed between the readability check and the read itself.
bool RawCopy(void* dst, const void* src, size_t bytes) noexcept
{
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#else
bool RawCopy(void* dst, const void* src, size_t bytes) noexcept
{
    std::memcpy(dst, src, bytes);
    return true;
}
#endif

/// Snapshot of every committed, readable page in the process. Consulted before
/// dereferencing a candidate pointer so the scan never touches a guard page.
class RegionMap
{
public:
    void Build()
    {
        m_ranges.clear();
        MEMORY_BASIC_INFORMATION mbi{};
        const uintptr_t limit  = MaxUserAddress();
        uintptr_t       cursor = kMinUserAddress;
        while (cursor < limit &&
               VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) == sizeof(mbi)) {
            const uintptr_t start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const uintptr_t end   = start + mbi.RegionSize;
            if (end <= cursor) break;
            if (IsReadable(mbi)) {
                if (!m_ranges.empty() && m_ranges.back().end == start) m_ranges.back().end = end;
                else                                                   m_ranges.push_back({start, end});
            }
            cursor = end;
        }
    }

    size_t Count() const { return m_ranges.size(); }

    bool Readable(uintptr_t addr, size_t bytes) const
    {
        if (addr < kMinUserAddress || bytes == 0) return false;
        const uintptr_t end = addr + bytes;
        if (end < addr) return false;

        auto it = std::upper_bound(m_ranges.begin(), m_ranges.end(), addr,
                                   [](uintptr_t value, const Range& r) { return value < r.start; });
        if (it == m_ranges.begin()) return false;
        --it;
        return addr >= it->start && end <= it->end;
    }

private:
    struct Range { uintptr_t start; uintptr_t end; };

    static bool IsReadable(const MEMORY_BASIC_INFORMATION& mbi)
    {
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        constexpr DWORD kReadableFlags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                         PAGE_EXECUTE_WRITECOPY;
        return (mbi.Protect & kReadableFlags) != 0;
    }

    std::vector<Range> m_ranges;
};

template <typename T>
bool Read(const RegionMap& regions, uintptr_t addr, T& out)
{
    if (!regions.Readable(addr, sizeof(T))) return false;
    return RawCopy(&out, reinterpret_cast<const void*>(addr), sizeof(T));
}

struct Section
{
    uintptr_t start = 0;
    uintptr_t end   = 0;

    bool Contains(uintptr_t addr) const { return addr >= start && addr < end; }
    bool Valid() const { return start != 0 && end > start; }
};

struct ModuleLayout
{
    uintptr_t base       = 0;
    uint64_t  image_size = 0;
    Section   rdata;
    Section   data;

    bool Valid() const { return base != 0 && rdata.Valid() && data.Valid(); }
};

bool ResolveModuleLayout(ModuleLayout& out)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!base) return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    out.base       = base;
    out.image_size = nt->OptionalHeader.SizeOfImage;

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9] = {};
        std::memcpy(name, section->Name, 8);

        const Section bounds{base + section->VirtualAddress,
                             base + section->VirtualAddress + section->Misc.VirtualSize};
        if (std::strcmp(name, ".rdata") == 0)     out.rdata = bounds;
        else if (std::strcmp(name, ".data") == 0) out.data  = bounds;
    }
    return out.Valid();
}

/// Layout of MSVC's RTTICompleteObjectLocator on x64.
struct RttiCompleteObjectLocator
{
    uint32_t signature;
    uint32_t offset;
    uint32_t cd_offset;
    uint32_t type_descriptor_rva;
    uint32_t class_descriptor_rva;
    uint32_t self_rva;
};

/// Walk vtable -> complete object locator -> type descriptor and compare the
/// mangled class name. The name lives in .data and survives game patches.
bool VTableClassNameMatches(const ModuleLayout& mod, const RegionMap& regions,
                            uintptr_t vtable, const char* mangled)
{
    uintptr_t locator_addr = 0;
    if (!Read(regions, vtable - sizeof(uintptr_t), locator_addr)) return false;
    if (!mod.rdata.Contains(locator_addr)) return false;

    RttiCompleteObjectLocator locator{};
    if (!Read(regions, locator_addr, locator)) return false;
    if (locator.signature != 1) return false;
    if (locator.self_rva != static_cast<uint32_t>(locator_addr - mod.base)) return false;

    const uintptr_t type_descriptor = mod.base + locator.type_descriptor_rva;
    if (!mod.data.Contains(type_descriptor)) return false;

    const size_t length = std::strlen(mangled);
    char actual[128] = {};
    if (length + 1 > sizeof(actual)) return false;
    if (!regions.Readable(type_descriptor + 0x10, length + 1)) return false;
    if (!RawCopy(actual, reinterpret_cast<const void*>(type_descriptor + 0x10), length + 1)) return false;

    return actual[length] == '\0' && std::memcmp(actual, mangled, length) == 0;
}

/// Visit every plausible object pointer stored in .data. The callback returns
/// true to stop the sweep.
template <typename Fn>
void ForEachDataPointer(const ModuleLayout& mod, const RegionMap& regions, Fn&& callback)
{
    for (uintptr_t page = mod.data.start & ~(kPageSize - 1); page < mod.data.end; page += kPageSize) {
        if (!regions.Readable(page, kPageSize)) continue;

        const uintptr_t page_end  = std::min(page + kPageSize, mod.data.end);
        const uintptr_t first     = std::max(page, mod.data.start);
        for (uintptr_t slot = (first + 7) & ~uintptr_t(7); slot + sizeof(uintptr_t) <= page_end;
             slot += sizeof(uintptr_t)) {
            uintptr_t value = 0;
            std::memcpy(&value, reinterpret_cast<const void*>(slot), sizeof(value));
            if (value < kMinUserAddress || value >= MaxUserAddress()) continue;
            if ((value & 7) != 0) continue;
            if (callback(slot, value)) return;
        }
    }
}

/// Read the level id out of a candidate session object. Doubles as the
/// structural predicate that identifies the object in the first place.
bool ReadSessionName(const RegionMap& regions, uintptr_t session, char* out, size_t capacity)
{
    uint32_t length = 0;
    if (!Read(regions, session + layout::kSessionNameLength, length)) return false;
    if (length < kLevelPrefixLength) return false;
    if (length > layout::kMaxInlineNameLength) return false;
    if (static_cast<size_t>(length) + 1 > capacity) return false;

    if (!regions.Readable(session + layout::kSessionName, length + 1)) return false;
    if (!RawCopy(out, reinterpret_cast<const void*>(session + layout::kSessionName), length + 1)) return false;

    if (out[length] != '\0') return false;
    if (std::strlen(out) != length) return false;
    return std::memcmp(out, kLevelPrefix, kLevelPrefixLength) == 0;
}

uintptr_t ResolveChassisBody(const RegionMap& regions, uintptr_t truck_control)
{
    uintptr_t sim_node = 0;
    if (!Read(regions, truck_control + layout::kTruckSimNode, sim_node) || !sim_node) return 0;

    uintptr_t child = 0;
    if (!Read(regions, sim_node + layout::kSimNodeChild, child) || !child) return 0;

    uintptr_t body = 0;
    if (!Read(regions, child + layout::kChildChassisBody, body) || !body) return 0;
    return body;
}

bool ChassisBodyLooksValid(const RegionMap& regions, uintptr_t body)
{
    float right[3]    = {};
    float position[3] = {};
    if (!Read(regions, body + layout::kBodyRightVector, right)) return false;
    if (!Read(regions, body + layout::kBodyWorldPosition, position)) return false;

    const float norm = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (!(norm > 0.99f && norm < 1.01f)) return false;

    for (float value : position) {
        if (!std::isfinite(value)) return false;
    }
    if (std::fabs(position[0]) > 20000.f || std::fabs(position[2]) > 20000.f) return false;
    return position[0] != 0.f || position[2] != 0.f;
}

/// Counters kept so a failed pass can say *why* it failed in the log.
struct ScanStats
{
    uint64_t data_pointers   = 0;
    uint64_t vtable_in_rdata = 0;
    uint64_t named_objects   = 0;
};

uintptr_t FindSessionGlobal(const ModuleLayout& mod, const RegionMap& regions, ScanStats& stats)
{
    uintptr_t found = 0;
    ForEachDataPointer(mod, regions, [&](uintptr_t slot, uintptr_t object) {
        ++stats.data_pointers;
        char name[layout::kMaxInlineNameLength + 1] = {};
        if (!ReadSessionName(regions, object, name, sizeof(name))) return false;
        found = slot - mod.base;
        SM_INFO("OffsetScanner: session candidate at +0x%llX holds '%s'.",
                static_cast<unsigned long long>(found), name);
        return true;
    });
    return found;
}

uintptr_t FindTruckControlByRtti(const ModuleLayout& mod, const RegionMap& regions, ScanStats& stats)
{
    uintptr_t found = 0;
    ForEachDataPointer(mod, regions, [&](uintptr_t slot, uintptr_t object) {
        uintptr_t vtable = 0;
        if (!Read(regions, object, vtable)) return false;
        if (!mod.rdata.Contains(vtable)) return false;
        ++stats.vtable_in_rdata;
        if (!VTableClassNameMatches(mod, regions, vtable, kTruckControlClass)) return false;
        ++stats.named_objects;
        found = slot - mod.base;
        return true;
    });
    return found;
}

/// True when the mangled class name is present in .data at all, i.e. the game
/// still ships RTTI. Used to decide whether a miss from the RTTI sweep means
/// "stripped" or merely "not spawned yet" — the latter must never fall back,
/// because the shape test alone will happily match some other physics body.
bool RttiNamePresentInData(const ModuleLayout& mod, const RegionMap& regions, const char* mangled)
{
    const size_t length = std::strlen(mangled);
    uintptr_t    run_begin = 0;

    for (uintptr_t page = mod.data.start & ~(kPageSize - 1); page < mod.data.end; page += kPageSize) {
        const bool readable = regions.Readable(page, kPageSize);
        if (readable && run_begin == 0) run_begin = std::max(page, mod.data.start);

        const bool last = (page + kPageSize) >= mod.data.end;
        if (run_begin != 0 && (!readable || last)) {
            const uintptr_t run_end = std::min(readable ? mod.data.end : page, mod.data.end);
            if (run_end > run_begin + length) {
                const auto*  bytes = reinterpret_cast<const unsigned char*>(run_begin);
                const size_t count = run_end - run_begin;
                for (size_t i = 0; i + length <= count; ++i) {
                    if (bytes[i] == static_cast<unsigned char>(mangled[0]) &&
                        std::memcmp(bytes + i, mangled, length) == 0) {
                        return true;
                    }
                }
            }
            run_begin = 0;
        }
    }
    return false;
}

/// Fallback for a future build with RTTI stripped: accept any global whose
/// pointer chain lands on something shaped like a chassis body.
uintptr_t FindTruckControlStructural(const ModuleLayout& mod, const RegionMap& regions)
{
    uintptr_t found = 0;
    ForEachDataPointer(mod, regions, [&](uintptr_t slot, uintptr_t object) {
        uintptr_t vtable = 0;
        if (!Read(regions, object, vtable)) return false;
        if (!mod.rdata.Contains(vtable)) return false;

        const uintptr_t body = ResolveChassisBody(regions, object);
        if (!body || !ChassisBodyLooksValid(regions, body)) return false;
        found = slot - mod.base;
        return true;
    });
    return found;
}

bool SessionOffsetValid(const ModuleLayout& mod, const RegionMap& regions, uintptr_t rva)
{
    if (!rva || rva >= mod.image_size) return false;

    uintptr_t session = 0;
    if (!Read(regions, mod.base + rva, session) || !session) return false;

    char name[layout::kMaxInlineNameLength + 1] = {};
    return ReadSessionName(regions, session, name, sizeof(name));
}

bool TruckOffsetValid(const ModuleLayout& mod, const RegionMap& regions, uintptr_t rva)
{
    if (!rva || rva >= mod.image_size) return false;

    uintptr_t truck_control = 0;
    if (!Read(regions, mod.base + rva, truck_control) || !truck_control) return false;

    uintptr_t vtable = 0;
    if (!Read(regions, truck_control, vtable) || !mod.rdata.Contains(vtable)) return false;
    if (VTableClassNameMatches(mod, regions, vtable, kTruckControlClass)) return true;

    const uintptr_t body = ResolveChassisBody(regions, truck_control);
    return body != 0 && ChassisBodyLooksValid(regions, body);
}

// ---------------------------------------------------------------------------
//  On-disk cache, keyed on the executable so a patched game misses the cache.
// ---------------------------------------------------------------------------

struct CacheKey
{
    uint64_t file_size  = 0;
    uint64_t write_time = 0;
    uint64_t image_size = 0;
};

bool BuildCacheKey(const ModuleLayout& mod, CacheKey& out)
{
    char exe[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return false;

    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExA(exe, GetFileExInfoStandard, &info)) return false;

    out.file_size  = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    out.write_time = (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
                     info.ftLastWriteTime.dwLowDateTime;
    out.image_size = mod.image_size;
    return true;
}

bool ResolveCachePath(char* out, size_t capacity)
{
    char exe[MAX_PATH] = {};
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return false;

    char* slash = std::strrchr(exe, '\\');
    if (!slash) return false;
    *slash = '\0';

    return std::snprintf(out, capacity, "%s\\SnowMap\\offsets.cache", exe) > 0;
}

bool LoadCache(const CacheKey& key, GameOffsets& out)
{
    char path[MAX_PATH] = {};
    if (!ResolveCachePath(path, sizeof(path))) return false;

    FILE* file = std::fopen(path, "r");
    if (!file) return false;

    char     tag[32]  = {};
    unsigned version  = 0;
    unsigned long long file_size = 0, write_time = 0, image_size = 0;
    unsigned long long session = 0, truck = 0;

    // Version 2 rejects caches written before the fallback gating fix, which
    // could have recorded a wrong TRUCK_CONTROL global.
    const bool parsed = std::fscanf(file, "%31s %u", tag, &version) == 2 &&
                        std::strcmp(tag, "snowmap-offsets") == 0 && version == 2 &&
                        std::fscanf(file, " key %llu %llu %llu", &file_size, &write_time, &image_size) == 3 &&
                        std::fscanf(file, " session %llx", &session) == 1 &&
                        std::fscanf(file, " truck %llx", &truck) == 1;
    std::fclose(file);

    if (!parsed) return false;
    if (file_size != key.file_size || write_time != key.write_time || image_size != key.image_size) return false;

    out.session_rva       = static_cast<uintptr_t>(session);
    out.truck_control_rva = static_cast<uintptr_t>(truck);
    return true;
}

void SaveCache(const CacheKey& key, const GameOffsets& offsets)
{
    char path[MAX_PATH] = {};
    if (!ResolveCachePath(path, sizeof(path))) return;

    FILE* file = std::fopen(path, "w");
    if (!file) {
        SM_WARN("OffsetScanner: could not write '%s'.", path);
        return;
    }
    std::fprintf(file, "snowmap-offsets 2\n");
    std::fprintf(file, "key %llu %llu %llu\n",
                 static_cast<unsigned long long>(key.file_size),
                 static_cast<unsigned long long>(key.write_time),
                 static_cast<unsigned long long>(key.image_size));
    std::fprintf(file, "session %llX\n", static_cast<unsigned long long>(offsets.session_rva));
    std::fprintf(file, "truck %llX\n", static_cast<unsigned long long>(offsets.truck_control_rva));
    std::fclose(file);
}

// ---------------------------------------------------------------------------
//  Worker state
// ---------------------------------------------------------------------------

std::mutex              g_mutex;
std::condition_variable g_wake;
bool                    g_stop    = false;
bool                    g_started = false;

std::atomic<uintptr_t> g_sessionRva{0};
std::atomic<uintptr_t> g_truckRva{0};
std::atomic<uintptr_t> g_moduleBase{0};

std::atomic<uint64_t> g_sessionFailingSince{0};
std::atomic<uint64_t> g_truckFailingSince{0};

void Invalidate(bool session, bool truck, const char* reason)
{
    if (session) g_sessionRva.store(0, std::memory_order_release);
    if (truck)   g_truckRva.store(0, std::memory_order_release);
    SM_WARN("OffsetScanner: rescanning (%s).", reason);
    g_wake.notify_all();
}

void RunDiscoveryPass(const ModuleLayout& mod, bool have_key, const CacheKey& key,
                      bool& cache_tried, uint32_t pass, bool verbose, bool rtti_present)
{
    RegionMap regions;
    regions.Build();

    GameOffsets offsets{g_sessionRva.load(std::memory_order_acquire),
                        g_truckRva.load(std::memory_order_acquire)};

    if (!cache_tried && have_key) {
        cache_tried = true;
        GameOffsets cached;
        if (!LoadCache(key, cached)) {
            SM_INFO("OffsetScanner: no usable cache for this build, scanning.");
        } else {
            const bool session_ok = SessionOffsetValid(mod, regions, cached.session_rva);
            const bool truck_ok   = TruckOffsetValid(mod, regions, cached.truck_control_rva);
            if (!offsets.session_rva && session_ok)      offsets.session_rva = cached.session_rva;
            if (!offsets.truck_control_rva && truck_ok)  offsets.truck_control_rva = cached.truck_control_rva;
            SM_INFO("OffsetScanner: cache hit (session=+0x%llX %s, truck=+0x%llX %s).",
                    static_cast<unsigned long long>(cached.session_rva), session_ok ? "valid" : "rejected",
                    static_cast<unsigned long long>(cached.truck_control_rva), truck_ok ? "valid" : "rejected");
        }
    }

    ScanStats stats;

    if (!offsets.session_rva) {
        offsets.session_rva = FindSessionGlobal(mod, regions, stats);
        if (offsets.session_rva)
            SM_INFO("OffsetScanner: session global at +0x%llX.",
                    static_cast<unsigned long long>(offsets.session_rva));
    }

    if (!offsets.truck_control_rva) {
        offsets.truck_control_rva = FindTruckControlByRtti(mod, regions, stats);
        if (offsets.truck_control_rva) {
            SM_INFO("OffsetScanner: TRUCK_CONTROL global at +0x%llX (RTTI).",
                    static_cast<unsigned long long>(offsets.truck_control_rva));
        } else if (!rtti_present) {
            // Only reachable on a build with no RTTI at all. While RTTI exists,
            // a miss just means the truck has not spawned yet — keep waiting.
            offsets.truck_control_rva = FindTruckControlStructural(mod, regions);
            if (offsets.truck_control_rva)
                SM_WARN("OffsetScanner: TRUCK_CONTROL global at +0x%llX (structural fallback, no RTTI in this build).",
                        static_cast<unsigned long long>(offsets.truck_control_rva));
        }
    }

    if (verbose && !offsets.Complete()) {
        SM_INFO("OffsetScanner: pass %u incomplete — session=%s truck=%s "
                "(regions=%zu, .data readable=%d, data pointers=%llu, "
                "vtables in .rdata=%llu, RTTI name matches=%llu).",
                pass,
                offsets.session_rva ? "found" : "MISSING",
                offsets.truck_control_rva ? "found" : "MISSING",
                regions.Count(),
                regions.Readable(mod.data.start, kPageSize) ? 1 : 0,
                static_cast<unsigned long long>(stats.data_pointers),
                static_cast<unsigned long long>(stats.vtable_in_rdata),
                static_cast<unsigned long long>(stats.named_objects));
    }

    g_sessionRva.store(offsets.session_rva, std::memory_order_release);
    g_truckRva.store(offsets.truck_control_rva, std::memory_order_release);

    if (offsets.Complete() && have_key) SaveCache(key, offsets);
}

void WorkerMain()
{
    ModuleLayout mod;
    if (!ResolveModuleLayout(mod)) {
        SM_ERROR("OffsetScanner: could not parse the game module headers; RAM polling disabled.");
        return;
    }
    g_moduleBase.store(mod.base, std::memory_order_release);

    SM_INFO("OffsetScanner: worker up. base=0x%llX image=%llu .rdata=[0x%llX,0x%llX) .data=[0x%llX,0x%llX).",
            static_cast<unsigned long long>(mod.base),
            static_cast<unsigned long long>(mod.image_size),
            static_cast<unsigned long long>(mod.rdata.start - mod.base),
            static_cast<unsigned long long>(mod.rdata.end - mod.base),
            static_cast<unsigned long long>(mod.data.start - mod.base),
            static_cast<unsigned long long>(mod.data.end - mod.base));

    CacheKey   key;
    const bool have_key = BuildCacheKey(mod, key);
    bool       cache_tried  = false;
    bool       rtti_probed  = false;
    bool       rtti_present = false;
    uint32_t   pass         = 0;

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            if (g_stop) return;
        }

        if (g_sessionRva.load(std::memory_order_acquire) == 0 ||
            g_truckRva.load(std::memory_order_acquire) == 0) {
            // Report the first few passes, then throttle to a heartbeat so a
            // long stay in the main menu does not flood the log.
            ++pass;
            if (!rtti_probed) {
                RegionMap probe;
                probe.Build();
                rtti_present = RttiNamePresentInData(mod, probe, kTruckControlClass);
                rtti_probed  = true;
                SM_INFO("OffsetScanner: RTTI %s in this build; structural fallback %s.",
                        rtti_present ? "present" : "absent",
                        rtti_present ? "disabled" : "enabled");
            }
            const bool verbose = pass <= 3 || (pass % kHeartbeatPasses) == 0;
            RunDiscoveryPass(mod, have_key, key, cache_tried, pass, verbose, rtti_present);
        }

        std::unique_lock<std::mutex> lock(g_mutex);
        g_wake.wait_for(lock, std::chrono::milliseconds(kRetryIntervalMs), [] { return g_stop; });
        if (g_stop) return;
    }
}

/// Shared bookkeeping for the two report entry points.
bool GraceExpired(std::atomic<uint64_t>& since, uint64_t grace_ms)
{
    const uint64_t now = GetTickCount64();
    uint64_t       start = since.load(std::memory_order_acquire);
    if (start == 0) {
        since.store(now, std::memory_order_release);
        return false;
    }
    if (now - start < grace_ms) return false;
    since.store(0, std::memory_order_release);
    return true;
}

} // namespace

void OffsetScanner::Start()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_started) return;
        g_started = true;
    }
    // Detached on purpose: joining from DllMain's process-detach path would take
    // the loader lock a second time and can deadlock.
    std::thread(WorkerMain).detach();
}

void OffsetScanner::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_started) return;
        g_stop = true;
    }
    g_wake.notify_all();
}

GameOffsets OffsetScanner::Current()
{
    return GameOffsets{g_sessionRva.load(std::memory_order_acquire),
                       g_truckRva.load(std::memory_order_acquire)};
}

uintptr_t OffsetScanner::SessionGlobal()
{
    const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
    const uintptr_t rva  = g_sessionRva.load(std::memory_order_acquire);
    return (base && rva) ? base + rva : 0;
}

uintptr_t OffsetScanner::TruckControlGlobal()
{
    const uintptr_t base = g_moduleBase.load(std::memory_order_acquire);
    const uintptr_t rva  = g_truckRva.load(std::memory_order_acquire);
    return (base && rva) ? base + rva : 0;
}

void OffsetScanner::ReportSessionResult(bool ok)
{
    if (ok) {
        g_sessionFailingSince.store(0, std::memory_order_release);
        return;
    }
    // The session global resolves in menus too, so sustained failure means the
    // module layout moved under us — discard both offsets.
    if (GraceExpired(g_sessionFailingSince, kSessionGraceMs))
        Invalidate(true, true, "session pointer stopped resolving");
}

void OffsetScanner::ReportTruckResult(bool ok, bool level_active)
{
    if (ok || !level_active) {
        g_truckFailingSince.store(0, std::memory_order_release);
        return;
    }
    if (GraceExpired(g_truckFailingSince, kTruckGraceMs))
        Invalidate(false, true, "truck chain stopped resolving on a loaded level");
}

} // namespace snowmap::game
