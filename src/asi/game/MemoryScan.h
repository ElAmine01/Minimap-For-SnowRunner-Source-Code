#pragma once
/// Memory primitives shared by the offset scanner and the entity tracker:
/// a snapshot of the process' readable pages, plus MSVC RTTI lookups on top.
///
/// Split out of OffsetScanner so both consumers agree on what "readable" means
/// and neither has to re-derive the RTTI walk.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace snowmap::game::scan {

inline constexpr uintptr_t kMinUserAddress = 0x10000;
inline constexpr uintptr_t kPageSize       = 0x1000;

/// Upper bound of the user address space. Must be queried, not guessed: the
/// game module itself loads around 0x7FF7'xxxx'xxxx, above any round constant
/// one is tempted to hardcode here.
uintptr_t MaxUserAddress();

/// memcpy that survives the region being freed between check and read.
bool RawCopy(void* dst, const void* src, size_t bytes) noexcept;

/// ReadProcessMemory-based read. Safe without a RegionMap, so this is what the
/// per-frame pollers use.
bool SafeRead(uintptr_t addr, void* dst, size_t size);

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

bool ResolveModuleLayout(ModuleLayout& out);

/// Snapshot of every committed, readable page in the process. Consulted before
/// dereferencing a candidate pointer so a scan never touches a guard page.
class RegionMap
{
public:
    struct Range { uintptr_t start; uintptr_t end; };

    void Build();

    size_t Count() const { return m_ranges.size(); }
    bool   Readable(uintptr_t addr, size_t bytes) const;

    /// Every readable range.
    const std::vector<Range>& Ranges() const { return m_ranges; }
    /// Readable ranges that are also private and writable, i.e. the heap.
    /// Object instances live here; images and file mappings do not.
    const std::vector<Range>& PrivateRanges() const { return m_private; }

    uint64_t PrivateBytes() const;

private:
    std::vector<Range> m_ranges;
    std::vector<Range> m_private;
};

template <typename T>
bool Read(const RegionMap& regions, uintptr_t addr, T& out)
{
    if (!regions.Readable(addr, sizeof(T))) return false;
    return RawCopy(&out, reinterpret_cast<const void*>(addr), sizeof(T));
}

/// Mangled class name behind a vtable, e.g. ".?AVTRUCK_CONTROL@combine@@".
bool ReadVTableClassName(const ModuleLayout& mod, const RegionMap& regions,
                         uintptr_t vtable, char* out, size_t capacity);

bool VTableClassNameMatches(const ModuleLayout& mod, const RegionMap& regions,
                            uintptr_t vtable, const char* mangled);

/// True when `mangled` appears anywhere in .data, i.e. the build ships RTTI.
bool RttiNamePresentInData(const ModuleLayout& mod, const RegionMap& regions,
                           const char* mangled);

/// Collect the address of every 8-aligned heap qword equal to `value`. Since a
/// vtable pointer sits at offset 0, a hit is the object's base address.
/// Stops at `cap` results.
void SweepPrivateForValue(const RegionMap& regions, uintptr_t value,
                          std::vector<uintptr_t>& out, size_t cap);

} // namespace snowmap::game::scan
