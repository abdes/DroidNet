//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>

namespace oxygen {

//! Mixin class that adds the ability to shutdown an object.
/*!
 \tparam Base The base class to mix-in the managed lifecycle.

\note Requires the Base class to have an `OnShutdown` method.
\note Requires the `MixinNamed` and `MixinInitialize` to be mixed in.
*/
// ReSharper disable once CppClassCanBeFinal (mixin should never be made final)
template <typename Base>
class MixinShutdown : public Base {
public:
    //! Forwarding constructor, for construction with arguments. \param
    /*!
     ctor_args The arguments passed to the constructor. The ones for which a
     type is specified in the template are stripped and stored for use by the
     `Initialize()` methods. The rest are forwarded to other mixins in the
     chain.
    */
    template <typename... Args>
    explicit MixinShutdown(Args... args)
        : Base(std::forward<Args>(args)...)
    {
    }

    //! Default constructor when CtorArgs is empty.
    MixinShutdown() = default;

    virtual ~MixinShutdown() noexcept = default;

    OXYGEN_DEFAULT_COPYABLE(MixinShutdown);
    OXYGEN_DEFAULT_MOVABLE(MixinShutdown);

    //! Shutdown.
    /*!
     \note This method will not do anything if the object is already shutdown.
     When it does do something, all resources should be released and the object
     should be in a state where it can be initialized again.

     \throws std::runtime_error if the shutdown fails. When that happens, every
     effort should be made to release incomplete resources, but the object may
     still end up in an inconsistent state, and should be reused only if
     appropriate.
    */
    void Shutdown()
    {
        if (!this->self().IsInitialized())
            return;

        LOG_F(INFO, "Shutting down {}", this->self().ObjectName());
        try {
            this->self().OnShutdown();
            this->self().IsInitialized(false);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string(this->self().ObjectName()) + " shutdown incomplete: "
                + std::string(e.what()));
        }
    }
};

} // namespace oxygen
