// M3 — NVN format → Vulkan format mapping (pure logic, host-testable).
#pragma once

#include "nvn/nvn_types.h"

#include <vulkan/vulkan.h>

namespace mng::nvn {

// Maps an NVN format to its Vulkan equivalent. Returns VK_FORMAT_UNDEFINED for
// formats without a mapping — the texture path must fail loudly on those.
VkFormat nvn_format_to_vk(Format f);

} // namespace mng::nvn
