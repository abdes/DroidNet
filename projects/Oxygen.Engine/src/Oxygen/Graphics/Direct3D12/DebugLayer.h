//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>
#include <dxgidebug.h>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/Mixin.h"
#include "Oxygen/Base/MixinInitialize.h"
#include "Oxygen/Base/MixinNamed.h"
#include "Oxygen/Base/MixinShutdown.h"

namespace oxygen::graphics::d3d12 {

//! Enable several debug layer features, including live object reporting, leak
//! tracking, and GPU-based validation.
class DebugLayer final
    : public Mixin<DebugLayer, Curry<MixinNamed, const char*>::mixin, MixinInitialize, MixinShutdown> {
public:
    //! Constructor to forward the arguments to the mixins in the chain.
    template <typename... Args>
    constexpr explicit DebugLayer(Args&&... args)
        : Mixin(std::forward<Args>(args)...)
    {
    }

    //! Set the object name.
    DebugLayer()
        : Mixin("Debug Layer")
    {
    }

    ~DebugLayer() override = default;

    OXYGEN_MAKE_NON_COPYABLE(DebugLayer);
    OXYGEN_MAKE_NON_MOVABLE(DebugLayer);

private:
    void OnInitialize(bool enable = true, bool enable_validation = false);
    template <typename Base, typename... CtorArgs>
    friend class MixinInitialize; //< Allow access to OnInitialize.

    void OnShutdown() noexcept;
    template <typename Base>
    friend class MixinShutdown; //< Allow access to OnShutdown.

    ID3D12Debug6* d3d12_debug_ {};
    IDXGIDebug1* dxgi_debug_ {};
    IDXGIInfoQueue* dxgi_info_queue_ {};
};

} // namespace oxygen
