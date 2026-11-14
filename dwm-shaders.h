#pragma once

#include "dxbc-checksum.h"

namespace dwmShaders {

struct Region {
    DWORD offset, size;
    std::array<Digest, modeCount> hash;
};
struct Edit {
    DWORD offset;
    std::array<Byte, selectableCount> value;
};
struct Plan {
    std::vector<Region> regions;
    std::vector<Edit> edits;
};

struct ShaderSignature {
    DWORD size;
    Digest hash;
    std::array<DWORD, 4> curveOffsets;
};
#include "tray-patch-plan.h"

template<class T>
T read(const Byte* bytes) {
    T value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

IMAGE_SECTION_HEADER readOnlyData(const std::vector<Byte>& disk) {
    auto invalid = [] { fail(L"Invalid x64 dwmcore.dll image layout", ERROR_BAD_EXE_FORMAT); };
    if (disk.size() < sizeof(IMAGE_DOS_HEADER)) invalid();
    const auto dos = read<IMAGE_DOS_HEADER>(disk.data());
    const size_t ntOffset = static_cast<size_t>(dos.e_lfanew);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || ntOffset > disk.size() ||
        sizeof(IMAGE_NT_HEADERS64) > disk.size() - ntOffset) invalid();
    const auto nt = read<IMAGE_NT_HEADERS64>(disk.data() + ntOffset);
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64)) invalid();
    const size_t table = ntOffset + sizeof(IMAGE_NT_HEADERS64);
    if (nt.FileHeader.NumberOfSections > (disk.size() - table) / sizeof(IMAGE_SECTION_HEADER)) invalid();
    IMAGE_SECTION_HEADER data{};
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        const auto section = read<IMAGE_SECTION_HEADER>(disk.data() + table + i * sizeof(IMAGE_SECTION_HEADER));
        if (std::memcmp(section.Name, ".rdata", 7) != 0 || section.Name[7] != 0) continue;
        constexpr DWORD flags = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE;
        if (data.SizeOfRawData || (section.Characteristics & flags) != IMAGE_SCN_MEM_READ ||
            !section.SizeOfRawData || section.PointerToRawData > disk.size() ||
            section.SizeOfRawData > disk.size() - section.PointerToRawData ||
            section.VirtualAddress > nt.OptionalHeader.SizeOfImage ||
            section.Misc.VirtualSize > nt.OptionalHeader.SizeOfImage - section.VirtualAddress) invalid();
        data = section;
    }
    if (!data.SizeOfRawData || !data.Misc.VirtualSize) invalid();
    return data;
}

bool validChecksum(const Byte* blob, size_t size) {
    const auto checksum = dxbcChecksum(blob, size);
    return std::memcmp(blob + 4, checksum.data(), sizeof(checksum)) == 0;
}

void refreshChecksum(Byte* blob, size_t size) {
    const auto checksum = dxbcChecksum(blob, size);
    std::memcpy(blob + 4, checksum.data(), sizeof(checksum));
}

void patchCurve(Byte* blob, const ShaderSignature& shader, float gamma) {
    constexpr std::array<float, 4> original{2.4f, 0.04045f, 0.055f, 0.94786733f};
    const std::array<float, 4> replacement{gamma, 0.0f, 0.0f, 1.0f};
    for (size_t i = 0; i < original.size(); ++i) {
        const auto offset = shader.curveOffsets[i];
        const float expected[]{original[i], original[i], original[i], 0.0f};
        if (offset > shader.size || sizeof(expected) > shader.size - offset ||
            std::memcmp(blob + offset, expected, sizeof(expected)) != 0)
            fail(L"Unexpected SDR shader constants", ERROR_INVALID_DATA);
        const float curve[]{replacement[i], replacement[i], replacement[i]};
        std::memcpy(blob + offset, curve, sizeof(curve));
    }
    refreshChecksum(blob, shader.size);
}

struct Target {
    DWORD offset;
    ShaderSignature signature;
};

// 辅助函数：在着色器 payload 内动态查找 4 组 sRGB 常数向量并提取偏移量
inline bool findSrgbCurveOffsets(const Byte* payload, DWORD size, std::array<DWORD, 4>& offsets) {
    constexpr std::array<float, 4> original{2.4f, 0.04045f, 0.055f, 0.94786733f};
    for (size_t i = 0; i < original.size(); ++i) {
        const float expected[]{original[i], original[i], original[i], 0.0f};
        bool found = false;
        // 浮点常数在 DXBC 中按 4 字节对齐
        for (DWORD off = 0; off + sizeof(expected) <= size; off += 4) {
            if (std::memcmp(payload + off, expected, sizeof(expected)) == 0) {
                offsets[i] = off;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

// 结合“静态白名单”与“强约束动态启发扫描”的双引擎匹配机制
std::vector<Target> matchLibrary(const Byte* blob, DWORD size, const Sha256& sha, bool allowDynamicScan) {
    const DWORD count = read<DWORD>(blob + 28);
    if (count < 2 || count > (size - 32) / sizeof(DWORD)) return {};
    DWORD next = 32 + count * sizeof(DWORD);
    
    struct Part { DWORD payload; DWORD length; DWORD fnIndex; };
    std::vector<Part> parts;
    parts.reserve(count);

    for (DWORD i = 0; i < count; ++i) {
        const DWORD part = read<DWORD>(blob + 32 + i * sizeof(DWORD));
        if (part != next || size - part < 8) return {};
        const DWORD length = read<DWORD>(blob + part + 4);
        const DWORD payload = part + 8;
        if (length > size - payload) return {};
        next = payload + length;
        if (i == count - 1) {
            if (std::memcmp(blob + part, "LIBH", 4) != 0 || next != size) return {};
            break;
        }
        if (std::memcmp(blob + part, "LIBF", 4) != 0) return {};
        parts.push_back({payload, length, i});
    }

    std::vector<Target> matches;

    // 引擎 1：优先执行已知静态白名单比对（最稳妥）
    for (const auto& p : parts) {
        for (size_t j = 0; j < ARRAYSIZE(shaders); ++j) {
            const auto& shader = shaders[j];
            if (p.length == shader.size && sha(blob + p.payload, p.length) == shader.hash) {
                matches.push_back({p.payload, shader});
                break;
            }
        }
    }
    if (!matches.empty()) return matches;

    // 引擎 2：Windows 更新后静态 Hash 改变时，激活“强约束动态启发扫描”
    // 约束：只在函数总数 >= 100 的 DWM 桌面合成主库中筛选核心管线着色器
    if (allowDynamicScan && count >= 100) {
        for (const auto& p : parts) {
            // 匹配主要色彩管线对应的接口索引 16 与 17
            if (p.fnIndex == 16 || p.fnIndex == 17) {
                std::array<DWORD, 4> offsets{};
                if (findSrgbCurveOffsets(blob + p.payload, p.length, offsets)) {
                    ShaderSignature dynamicSig{p.length, sha(blob + p.payload, p.length), offsets};
                    matches.push_back({p.payload, dynamicSig});
                }
            }
        }
    }

    return matches;
}

Plan locate(const std::vector<Byte>& disk, const Sha256& sha) {
    const auto section = readOnlyData(disk);
    const auto* data = disk.data() + section.PointerToRawData;
    const DWORD length = (std::min)(section.SizeOfRawData, section.Misc.VirtualSize);
    constexpr float gamma[]{2.2f, 2.4f, 1.0f, 2.6f};
    static_assert(ARRAYSIZE(gamma) == modeCount - 1);

    // 首先尝试严格静态匹配；若因系统更新未完全命中 6 个，则无缝切换到强约束动态适配引擎
    for (int pass = 0; pass < 2; ++pass) {
        const bool allowDynamic = (pass == 1);
        Plan plan;
        size_t totalMatchedShaders = 0;

        for (DWORD offset = 0; length - offset >= 32; ++offset) {
            const auto* blob = data + offset;
            if (std::memcmp(blob, "DXBC", 4) != 0 || read<DWORD>(blob + 20) != 1) continue;
            const DWORD size = read<DWORD>(blob + 24);
            if (size < 32 || size > length - offset) continue;
            const auto matches = matchLibrary(blob, size, sha, allowDynamic);
            if (matches.empty()) continue;
            if (!validChecksum(blob, size)) fail(L"Invalid DWM shader library checksum", ERROR_INVALID_DATA);

            totalMatchedShaders += matches.size();
            const DWORD rva = section.VirtualAddress + offset;
            Region region{rva, size, {}};
            region.hash[0] = sha(blob, size);
            std::array<std::vector<Byte>, selectableCount - 1> variants;
            for (size_t i = 0; i < ARRAYSIZE(gamma); ++i) {
                std::vector<Byte> patched(blob, blob + size);
                for (const auto& target : matches)
                    patchCurve(patched.data() + target.offset, target.signature, gamma[i]);
                refreshChecksum(patched.data(), size);
                region.hash[i + 1] = sha(patched);
                if (i < variants.size()) variants[i] = std::move(patched);
            }
            plan.regions.push_back(region);
            for (DWORD i = 0; i < size; ++i) {
                Edit edit{rva + i, {blob[i], variants[0][i], variants[1][i], variants[2][i]}};
                if (edit.value[0] != edit.value[1] || edit.value[0] != edit.value[2] || edit.value[0] != edit.value[3])
                    plan.edits.push_back(edit);
            }
            offset += size - 1;
        }

        // 强约束安全检查：必须恰好命中 6 个核心目标着色器（3个库各自的2个转换函数）
        if (totalMatchedShaders == 6 && plan.regions.size() == 3) {
            return plan;
        }
    }

    fail(L"Could not locate all six verified SDR shaders in dwmcore.dll", ERROR_REVISION_MISMATCH);
}

} // namespace dwmShaders
