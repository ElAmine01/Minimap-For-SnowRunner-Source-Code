#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MemoryScan.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace snowmap::game::scan {
namespace {

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

uintptr_t ResolveTypeDescriptor(const ModuleLayout& mod, const RegionMap& regions, uintptr_t vtable)
{
    uintptr_t locator_addr = 0;
    if (!Read(regions, vtable - sizeof(uintptr_t), locator_addr)) return 0;
    if (!mod.rdata.Contains(locator_addr)) return 0;

    RttiCompleteObjectLocator locator{};
    if (!Read(regions, locator_addr, locator)) return 0;
    if (locator.signature != 1) return 0;
    if (locator.self_rva != static_cast<uint32_t>(locator_addr - mod.base)) return 0;

    const uintptr_t type_descriptor = mod.base + locator.type_descriptor_rva;
    if (!mod.data.Contains(type_descriptor)) return 0;
    return type_descriptor;
}

bool IsReadable(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    constexpr DWORD kReadableFlags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                     PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                     PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & kReadableFlags) != 0;
}

bool IsPrivateWritable(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.Type != MEM_PRIVATE) return false;
    constexpr DWORD kWritableFlags = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                                     PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & kWritableFlags) != 0;
}

void Append(std::vector<RegionMap::Range>& ranges, uintptr_t start, uintptr_t end)
{
    if (!ranges.empty() && ranges.back().end == start) ranges.back().end = end;
    else                                               ranges.push_back({start, end});
}

/// One guarded pass over a block of qwords. An exception costs the block, not
/// the sweep: pages get freed underneath us while the game runs.
size_t ScanBlock(const uintptr_t* base, size_t count, uintptr_t value,
                 uintptr_t* out, size_t cap) noexcept
{
    __try {
        size_t found = 0;
        for (size_t i = 0; i < count && found < cap; ++i) {
            if (base[i] == value) out[found++] = reinterpret_cast<uintptr_t>(base + i);
        }
        return found;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

constexpr size_t kScanBlockQwords = 8192;   // 64 KiB per guarded block

} // namespace

uintptr_t MaxUserAddress()
{
    static const uintptr_t cached = [] {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    }();
    return cached;
}

#if defined(_MSC_VER)
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

bool SafeRead(uintptr_t addr, void* dst, size_t size)
{
    if (!addr) return false;
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr),
                             dst, size, &read) && read == size;
}

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

void RegionMap::Build()
{
    m_ranges.clear();
    m_private.clear();

    MEMORY_BASIC_INFORMATION mbi{};
    const uintptr_t limit  = MaxUserAddress();
    uintptr_t       cursor = kMinUserAddress;

    while (cursor < limit &&
           VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const uintptr_t start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t end   = start + mbi.RegionSize;
        if (end <= cursor) break;

        if (IsReadable(mbi)) {
            Append(m_ranges, start, end);
            if (IsPrivateWritable(mbi)) Append(m_private, start, end);
        }
        cursor = end;
    }
}

bool RegionMap::Readable(uintptr_t addr, size_t bytes) const
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

uint64_t RegionMap::PrivateBytes() const
{
    uint64_t total = 0;
    for (const Range& r : m_private) total += r.end - r.start;
    return total;
}

bool ReadVTableClassName(const ModuleLayout& mod, const RegionMap& regions,
                         uintptr_t vtable, char* out, size_t capacity)
{
    if (!out || capacity < 2) return false;
    out[0] = '\0';

    const uintptr_t type_descriptor = ResolveTypeDescriptor(mod, regions, vtable);
    if (!type_descriptor) return false;

    const uintptr_t name = type_descriptor + 0x10;
    size_t chunk = capacity - 1;
    while (chunk > 0 && !regions.Readable(name, chunk)) chunk /= 2;
    if (chunk == 0) return false;

    if (!RawCopy(out, reinterpret_cast<const void*>(name), chunk)) return false;
    out[chunk] = '\0';
    return out[0] != '\0';
}

bool VTableClassNameMatches(const ModuleLayout& mod, const RegionMap& regions,
                            uintptr_t vtable, const char* mangled)
{
    const uintptr_t type_descriptor = ResolveTypeDescriptor(mod, regions, vtable);
    if (!type_descriptor) return false;

    const size_t length = std::strlen(mangled);
    char actual[128] = {};
    if (length + 1 > sizeof(actual)) return false;
    if (!regions.Readable(type_descriptor + 0x10, length + 1)) return false;
    if (!RawCopy(actual, reinterpret_cast<const void*>(type_descriptor + 0x10), length + 1)) return false;

    return actual[length] == '\0' && std::memcmp(actual, mangled, length) == 0;
}

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

void SweepPrivateForValue(const RegionMap& regions, uintptr_t value,
                          std::vector<uintptr_t>& out, size_t cap)
{
    out.clear();
    if (!value || cap == 0) return;

    for (const RegionMap::Range& range : regions.PrivateRanges()) {
        uintptr_t cursor = (range.start + 7) & ~uintptr_t(7);
        while (cursor + sizeof(uintptr_t) <= range.end) {
            const size_t remaining = static_cast<size_t>(range.end - cursor) / sizeof(uintptr_t);
            const size_t count     = std::min(remaining, kScanBlockQwords);

            // Sized so a dense block of instances is not silently truncated.
            uintptr_t hits[256];
            const size_t room  = std::min(cap - out.size(), sizeof(hits) / sizeof(hits[0]));
            const size_t found = ScanBlock(reinterpret_cast<const uintptr_t*>(cursor),
                                           count, value, hits, room);
            out.insert(out.end(), hits, hits + found);
            if (out.size() >= cap) return;

            cursor += count * sizeof(uintptr_t);
        }
    }
}

} // namespace snowmap::game::scan
