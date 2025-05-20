//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Graphics/Common/Sampler.h>

namespace oxygen::graphics {

size_t SamplerDesc::GetHash() const noexcept
{
    size_t hash = 0;
    HashCombine(hash, static_cast<uint8_t>(filter));
    HashCombine(hash, static_cast<uint8_t>(address_u));
    HashCombine(hash, static_cast<uint8_t>(address_v));
    HashCombine(hash, static_cast<uint8_t>(address_w));
    HashCombine(hash, mip_lod_bias);
    HashCombine(hash, max_anisotropy);
    HashCombine(hash, static_cast<uint8_t>(compare_func));
    HashCombine(hash, border_color[0]);
    HashCombine(hash, border_color[1]);
    HashCombine(hash, border_color[2]);
    HashCombine(hash, border_color[3]);
    HashCombine(hash, min_lod);
    HashCombine(hash, max_lod);
    return hash;
}

Sampler::Sampler(const SamplerDesc& desc)
    : desc_(desc)
{
    // Implementation will be provided by backend-specific implementations
    // that create the appropriate native resource
}

Sampler::Sampler(NativeObject native_handle, const SamplerDesc& desc)
    : native_(native_handle)
    , desc_(desc)
{
}

Sampler::~Sampler()
{
    // Backend-specific cleanup will be implemented in derived classes
}

// Common predefined samplers
namespace Samplers {

    Sampler PointClamp()
    {
        SamplerDesc desc;
        desc.filter = SamplerDesc::Filter::kPoint;
        desc.address_u = SamplerDesc::AddressMode::kClamp;
        desc.address_v = SamplerDesc::AddressMode::kClamp;
        desc.address_w = SamplerDesc::AddressMode::kClamp;
        return Sampler(desc);
    }

    Sampler BilinearClamp()
    {
        SamplerDesc desc;
        desc.filter = SamplerDesc::Filter::kBilinear;
        desc.address_u = SamplerDesc::AddressMode::kClamp;
        desc.address_v = SamplerDesc::AddressMode::kClamp;
        desc.address_w = SamplerDesc::AddressMode::kClamp;
        return Sampler(desc);
    }

    Sampler TrilinearWrap()
    {
        SamplerDesc desc;
        desc.filter = SamplerDesc::Filter::kTrilinear;
        desc.address_u = SamplerDesc::AddressMode::kWrap;
        desc.address_v = SamplerDesc::AddressMode::kWrap;
        desc.address_w = SamplerDesc::AddressMode::kWrap;
        return Sampler(desc);
    }

    Sampler AnisotropicWrap(uint32_t max_anisotropy)
    {
        SamplerDesc desc;
        desc.filter = SamplerDesc::Filter::kAniso;
        desc.address_u = SamplerDesc::AddressMode::kWrap;
        desc.address_v = SamplerDesc::AddressMode::kWrap;
        desc.address_w = SamplerDesc::AddressMode::kWrap;
        desc.max_anisotropy = max_anisotropy;
        return Sampler(desc);
    }

    Sampler ShadowComparison()
    {
        SamplerDesc desc;
        desc.filter = SamplerDesc::Filter::kBilinear;
        desc.address_u = SamplerDesc::AddressMode::kClamp;
        desc.address_v = SamplerDesc::AddressMode::kClamp;
        desc.address_w = SamplerDesc::AddressMode::kClamp;
        desc.compare_func = SamplerDesc::CompareFunc::kLess;
        return Sampler(desc);
    }

} // namespace Samplers

} // namespace oxygen::graphics
