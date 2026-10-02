// engine/render/src/spirv_contract.cpp
// Bounded reflection for Hue's fixed layouts, after pinned SPIRV-Tools validation.
// This is intentionally not a general-purpose reflection library.
#include "hue/core/log.h"
#include "hue/core/memory.h"
#include "hue/core/trace.h"
#include "hue/render/spirv.h"
#include <cstring>
#include <spirv-tools/libspirv.h>
#include <spirv/unified1/spirv.hpp>

namespace hue {
namespace {
constexpr std::uint32_t kAbsent = UINT32_MAX;
constexpr std::uint32_t kMaxIds = 4096;
struct Id {
    std::uint32_t at = 0, location = kAbsent, set = kAbsent, binding = kAbsent, stride = 0;
    bool builtin = false, block = false;
};
struct Reflection {
    const std::uint32_t* w;
    std::size_t count;
    Id* ids;
    std::uint32_t bound;
    bool is(std::uint32_t id, spv::Op op, unsigned words = 0) const {
        return id < bound && ids[id].at && (w[ids[id].at] & 65535u) == static_cast<unsigned>(op) &&
               (!words || (w[ids[id].at] >> 16) == words);
    }
    const std::uint32_t* node(std::uint32_t id) const { return w + ids[id].at; }
    bool scalar(std::uint32_t id, bool integer = false) const {
        if (integer)
            return is(id, spv::OpTypeInt, 4) && node(id)[2] == 32 && node(id)[3] == 0;
        return is(id, spv::OpTypeFloat, 3) && node(id)[2] == 32;
    }
    bool vector(std::uint32_t id, unsigned n, bool integer = false) const {
        return is(id, spv::OpTypeVector, 4) && node(id)[3] == n && scalar(node(id)[2], integer);
    }
    bool matrix(std::uint32_t id) const {
        return is(id, spv::OpTypeMatrix, 4) && node(id)[3] == 4 && vector(node(id)[2], 4);
    }
    bool member(std::uint32_t id, unsigned m, spv::Decoration decoration,
                unsigned value = kAbsent) const {
        for (std::size_t p = 5; p < count; p += w[p] >> 16) {
            const auto n = w[p] >> 16;
            if ((w[p] & 65535u) == spv::OpMemberDecorate && n >= 4 && w[p + 1] == id &&
                w[p + 2] == m && w[p + 3] == static_cast<unsigned>(decoration))
                return value == kAbsent ? n == 4 : n == 5 && w[p + 4] == value;
        }
        return false;
    }
    bool mat_member(std::uint32_t id, unsigned m) const {
        return member(id, m, spv::DecorationColMajor) &&
               member(id, m, spv::DecorationMatrixStride, 16);
    }
    bool array4(std::uint32_t id) const {
        if (!is(id, spv::OpTypeArray, 4) || ids[id].stride != 16 || !vector(node(id)[2], 4))
            return false;
        const auto length = node(id)[3];
        return is(length, spv::OpConstant, 4) && node(length)[3] == 4;
    }
    bool frame(std::uint32_t id) const {
        if (!is(id, spv::OpTypeStruct, 9) || !ids[id].block)
            return false;
        const auto* n = node(id);
        if (!matrix(n[2]) || !mat_member(id, 0) || !member(id, 0, spv::DecorationOffset, 0))
            return false;
        for (unsigned i = 1; i < 5; ++i)
            if (!vector(n[2 + i], 4) || !member(id, i, spv::DecorationOffset, 64 + (i - 1) * 16))
                return false;
        return array4(n[7]) && array4(n[8]) && member(id, 5, spv::DecorationOffset, 128) &&
               member(id, 6, spv::DecorationOffset, 192);
    }
    bool skin(std::uint32_t id) const {
        if (!is(id, spv::OpTypeStruct, 3) || !ids[id].block ||
            !member(id, 0, spv::DecorationOffset, 0) || !mat_member(id, 0) ||
            !member(id, 0, spv::DecorationNonWritable))
            return false;
        const auto a = node(id)[2];
        return is(a, spv::OpTypeRuntimeArray, 3) && ids[a].stride == 64 && matrix(node(a)[2]);
    }
    bool sampler(std::uint32_t id) const {
        if (!is(id, spv::OpTypeSampledImage, 3))
            return false;
        const auto a = node(id)[2];
        if (!is(a, spv::OpTypeImage, 9))
            return false;
        const auto* n = node(a);
        return scalar(n[2]) && n[3] == 1 && n[4] == 0 && n[5] == 0 && n[6] == 0 && n[7] == 1 &&
               n[8] == 0;
    }
    bool push(std::uint32_t id, bool hud, bool fragment) const {
        if (!is(id, spv::OpTypeStruct, hud ? 6 : fragment ? 5 : 6) || !ids[id].block)
            return false;
        const auto* n = node(id);
        if (hud)
            return vector(n[2], 2) && vector(n[3], 2) && vector(n[4], 4) && vector(n[5], 2) &&
                   member(id, 0, spv::DecorationOffset, 0) &&
                   member(id, 1, spv::DecorationOffset, 8) &&
                   member(id, 2, spv::DecorationOffset, 16) &&
                   member(id, 3, spv::DecorationOffset, 32);
        return matrix(n[2]) && mat_member(id, 0) && vector(n[3], 4) && vector(n[4], 4) &&
               member(id, 0, spv::DecorationOffset, 0) &&
               member(id, 1, spv::DecorationOffset, 64) &&
               member(id, 2, spv::DecorationOffset, 80) &&
               (fragment || (vector(n[5], 4, true) && member(id, 3, spv::DecorationOffset, 96)));
    }
};
} // namespace
Result<void> validate_shader_contract(const void* bytes, std::size_t size,
                                      const char* name) noexcept {
    HUE_PROFILE_ZONE("render::validate_shader_contract");
    if (!name || !validate_spirv_bytes(bytes, size) || size > 128u * 1024u)
        return ErrorCode::kCorruptData;
    const bool mesh_v = !std::strcmp(name, "mesh.vert.spv"),
               skin_v = !std::strcmp(name, "skinned_mesh.vert.spv");
    const bool mesh_f = !std::strcmp(name, "mesh.frag.spv"),
               hud_v = !std::strcmp(name, "hud.vert.spv");
    const bool hud_f = !std::strcmp(name, "hud.frag.spv"),
               tri_v = !std::strcmp(name, "triangle.vert.spv");
    const bool tri_f = !std::strcmp(name, "triangle.frag.spv");
    if (!(mesh_v || skin_v || mesh_f || hud_v || hud_f || tri_v || tri_f))
        return ErrorCode::kUnsupported;
    auto arena = LinearArena::create(512u * 1024u, MemoryTag::kRender);
    if (!arena)
        return arena.error();
    auto words = arena.value().allocate<std::uint32_t>(size / 4);
    auto storage = arena.value().allocate<Id>(kMaxIds);
    if (!words || !storage)
        return ErrorCode::kOutOfMemory;
    std::memcpy(words.value(), bytes, size);
    const auto* w = words.value();
    const auto bound = w[3];
    if (w[0] != 0x07230203 || !bound || bound > kMaxIds || w[4] != 0)
        return ErrorCode::kCorruptData;
    for (unsigned i = 0; i < bound; ++i)
        storage.value()[i] = Id{};
    Reflection r{w, size / 4, storage.value(), bound};
    // Bound the instruction walk before invoking the semantic validator.
    unsigned entry_count = 0;
    for (std::size_t p = 5; p < r.count;) {
        const auto n = w[p] >> 16, op = w[p] & 65535u;
        if (!n || n > r.count - p)
            return ErrorCode::kCorruptData;
        if (op == spv::OpCapability && (n != 2 || w[p + 1] != spv::CapabilityShader))
            return ErrorCode::kUnsupported;
        if (op == spv::OpExtension)
            return ErrorCode::kUnsupported;
        if (op == spv::OpEntryPoint) {
            if (++entry_count != 1 || n < 5 ||
                w[p + 1] != static_cast<unsigned>((mesh_f || hud_f || tri_f)
                                                      ? spv::ExecutionModelFragment
                                                      : spv::ExecutionModelVertex) ||
                w[p + 3] != 0x6e69616d || w[p + 4] != 0)
                return ErrorCode::kCorruptData;
        }
        p += n;
    }
    if (entry_count != 1)
        return ErrorCode::kCorruptData;
    const auto context = spvContextCreate(SPV_ENV_VULKAN_1_3);
    if (!context)
        return ErrorCode::kOutOfMemory;
    spv_diagnostic diagnostic = nullptr;
    const auto result = spvValidateBinary(context, w, r.count, &diagnostic);
    if (result != SPV_SUCCESS && diagnostic)
        HUE_LOG_WARN("shader validation: %.200s", diagnostic->error);
    spvDiagnosticDestroy(diagnostic);
    spvContextDestroy(context);
    if (result != SPV_SUCCESS)
        return ErrorCode::kCorruptData;
    for (std::size_t p = 5; p < r.count; p += w[p] >> 16) {
        const auto n = w[p] >> 16, op = w[p] & 65535u;
        unsigned id = 0;
        if (op >= spv::OpTypeVoid && op <= spv::OpTypeFunction)
            id = w[p + 1];
        if (op == spv::OpVariable || op == spv::OpConstant)
            id = w[p + 2];
        if (id) {
            if (id >= bound)
                return ErrorCode::kCorruptData;
            r.ids[id].at = static_cast<std::uint32_t>(p);
        }
        if (op == spv::OpDecorate) {
            if (w[p + 1] >= bound || n < 3)
                return ErrorCode::kCorruptData;
            auto& d = r.ids[w[p + 1]];
            switch (w[p + 2]) {
            case spv::DecorationLocation:
                if (n != 4)
                    return ErrorCode::kCorruptData;
                d.location = w[p + 3];
                break;
            case spv::DecorationDescriptorSet:
                if (n != 4)
                    return ErrorCode::kCorruptData;
                d.set = w[p + 3];
                break;
            case spv::DecorationBinding:
                if (n != 4)
                    return ErrorCode::kCorruptData;
                d.binding = w[p + 3];
                break;
            case spv::DecorationArrayStride:
                if (n != 4)
                    return ErrorCode::kCorruptData;
                d.stride = w[p + 3];
                break;
            case spv::DecorationBuiltIn:
                d.builtin = true;
                break;
            case spv::DecorationBlock:
                d.block = true;
                break;
            case spv::DecorationComponent:
            case spv::DecorationIndex:
            case spv::DecorationFlat:
            case spv::DecorationNoPerspective:
                return ErrorCode::kUnsupported;
            default:
                break;
            }
        }
        if (op == spv::OpDecorationGroup || op == spv::OpGroupDecorate ||
            op == spv::OpGroupMemberDecorate)
            return ErrorCode::kUnsupported;
    }
    unsigned inputs = 0, outputs = 0, descriptors = 0, pushes = 0;
    for (unsigned id = 1; id < bound; ++id) {
        if (!r.is(id, spv::OpVariable))
            continue;
        const auto* v = r.node(id);
        const auto storage_class = v[3];
        if (storage_class == spv::StorageClassFunction || storage_class == spv::StorageClassPrivate)
            continue;
        if (!r.is(v[1], spv::OpTypePointer, 4))
            return ErrorCode::kCorruptData;
        const auto type = r.node(v[1])[3];
        const auto& d = r.ids[id];
        if (storage_class == spv::StorageClassInput || storage_class == spv::StorageClassOutput) {
            if (d.builtin)
                continue;
            // gl_PerVertex is a compiler-defined built-in block, not a user location.
            if (r.is(type, spv::OpTypeStruct) && r.ids[type].block) {
                const auto* s = r.node(type);
                for (unsigned m = 0; m < (s[0] >> 16) - 2; ++m) {
                    bool found = false;
                    for (std::size_t p = 5; p < r.count; p += w[p] >> 16)
                        if ((w[p] & 65535u) == spv::OpMemberDecorate && (w[p] >> 16) == 5 &&
                            w[p + 1] == type && w[p + 2] == m && w[p + 3] == spv::DecorationBuiltIn)
                            found = true;
                    if (!found)
                        return ErrorCode::kCorruptData;
                }
                continue;
            }
            if (d.location > 4)
                return ErrorCode::kCorruptData;
            const bool input = storage_class == spv::StorageClassInput;
            const unsigned bit = 1u << d.location;
            auto& mask = input ? inputs : outputs;
            if (mask & bit)
                return ErrorCode::kCorruptData;
            unsigned width = 0;
            bool integer = false;
            if (input && (mesh_v || skin_v)) {
                const unsigned widths[] = {3, 3, 2, 4, 4};
                width = widths[d.location];
                integer = d.location == 3;
                if (mesh_v && d.location > 2)
                    return ErrorCode::kCorruptData;
            } else if ((input && mesh_f) || (!input && (mesh_v || skin_v))) {
                if (d.location > 2)
                    return ErrorCode::kCorruptData;
                width = d.location == 2 ? 2 : 3;
            } else if (d.location == 0)
                width = (tri_v || (tri_f && input)) ? 3 : 4;
            if (!width || !r.vector(type, width, integer))
                return ErrorCode::kCorruptData;
            mask |= bit;
        } else if (storage_class == spv::StorageClassPushConstant) {
            if (++pushes > 1 || !(mesh_v || skin_v || mesh_f || hud_v) ||
                !r.push(type, hud_v, mesh_f))
                return ErrorCode::kCorruptData;
        } else {
            unsigned bit = 0;
            if (d.set == 0 && d.binding == 0 && storage_class == spv::StorageClassUniform &&
                (mesh_v || skin_v || mesh_f) && r.frame(type))
                bit = 1;
            if (d.set == 1 && d.binding < 2 && storage_class == spv::StorageClassUniformConstant &&
                mesh_f && r.sampler(type))
                bit = 2u << d.binding;
            if (d.set == 2 && d.binding == 0 && storage_class == spv::StorageClassStorageBuffer &&
                skin_v && r.skin(type))
                bit = 8;
            if (!bit || (descriptors & bit))
                return ErrorCode::kCorruptData;
            descriptors |= bit;
        }
    }
    const unsigned expected_in = skin_v               ? 31u
                                 : (mesh_v || mesh_f) ? 7u
                                 : (tri_f || hud_f)   ? 1u
                                                      : 0u;
    const unsigned expected_out = (mesh_v || skin_v) ? 7u : 1u;
    const unsigned expected_descriptors = skin_v ? 9u : mesh_v ? 1u : mesh_f ? 7u : 0u;
    const unsigned expected_push = (mesh_v || skin_v || mesh_f || hud_v) ? 1u : 0u;
    if (inputs != expected_in || outputs != expected_out || descriptors != expected_descriptors ||
        pushes != expected_push)
        return ErrorCode::kCorruptData;
    return {};
}
} // namespace hue
