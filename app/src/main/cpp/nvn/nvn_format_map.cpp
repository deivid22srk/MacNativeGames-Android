// M3 — NVN format → Vulkan format mapping.
// Sources: NVN SDK enum values (games/totk/third_party/d2r_gfx/gfx/nvn) and the
// Vulkan spec. Compressed formats keep their native block encodings — both APIs
// use the same BCn bitstreams, so no transcoding is needed.
#include "nvn/nvn_format_map.h"

namespace mng::nvn {

VkFormat nvn_format_to_vk(Format f) {
    switch (f) {
    case Format::R8_UNORM:       return VK_FORMAT_R8_UNORM;
    case Format::R8_SNORM:       return VK_FORMAT_R8_SNORM;
    case Format::RG8_UNORM:      return VK_FORMAT_R8G8_UNORM;
    case Format::RGBA8_UNORM:    return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SRGB:     return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGR565:         return VK_FORMAT_B5G6R5_UNORM_PACK16;
    case Format::RGB5A1:         return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case Format::RGB10A2:        return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::R16_UNORM:      return VK_FORMAT_R16_UNORM;
    case Format::R16_FLOAT:      return VK_FORMAT_R16_SFLOAT;
    case Format::RG16_FLOAT:     return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16_FLOAT:   return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32_FLOAT:      return VK_FORMAT_R32_SFLOAT;
    case Format::RG32_FLOAT:     return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGBA32_FLOAT:   return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::D32_FLOAT:      return VK_FORMAT_D32_SFLOAT;
    case Format::D24S8_UNORM:    return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::BC1_RGBA_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1_RGBA_SRGB:  return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC3_UNORM:      return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3_SRGB:       return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::BC5_UNORM:      return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC7_UNORM:      return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::BC7_SRGB:       return VK_FORMAT_BC7_SRGB_BLOCK;
    default:                     return VK_FORMAT_UNDEFINED;
    }
}

const FormatInfo* format_info(Format f) {
    static const struct Entry {
        Format f;
        FormatInfo info;
    } kTable[] = {
        {Format::R8_UNORM,       {1, 1, 1, false}},
        {Format::R8_SNORM,       {1, 1, 1, false}},
        {Format::RG8_UNORM,      {2, 1, 1, false}},
        {Format::RGBA8_UNORM,    {4, 1, 1, false}},
        {Format::RGBA8_SRGB,     {4, 1, 1, false}},
        {Format::BGR565,         {2, 1, 1, false}},
        {Format::RGB5A1,         {2, 1, 1, false}},
        {Format::RGB10A2,        {4, 1, 1, false}},
        {Format::R16_UNORM,      {2, 1, 1, false}},
        {Format::R16_FLOAT,      {2, 1, 1, false}},
        {Format::RG16_FLOAT,     {4, 1, 1, false}},
        {Format::RGBA16_FLOAT,   {8, 1, 1, false}},
        {Format::R32_FLOAT,      {4, 1, 1, false}},
        {Format::RG32_FLOAT,     {8, 1, 1, false}},
        {Format::RGBA32_FLOAT,   {16, 1, 1, false}},
        {Format::D32_FLOAT,      {4, 1, 1, false}},
        {Format::D24S8_UNORM,    {4, 1, 1, false}},
        {Format::BC1_RGBA_UNORM, {8, 4, 4, true}},
        {Format::BC1_RGBA_SRGB,  {8, 4, 4, true}},
        {Format::BC3_UNORM,      {16, 4, 4, true}},
        {Format::BC3_SRGB,       {16, 4, 4, true}},
        {Format::BC5_UNORM,      {16, 4, 4, true}},
        {Format::BC7_UNORM,      {16, 4, 4, true}},
        {Format::BC7_SRGB,       {16, 4, 4, true}},
    };
    for (const auto& e : kTable) {
        if (e.f == f) return &e.info;
    }
    return nullptr;
}

} // namespace mng::nvn
