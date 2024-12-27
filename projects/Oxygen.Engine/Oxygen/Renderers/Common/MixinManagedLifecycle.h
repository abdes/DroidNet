//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "Oxygen/Base/Logging.h"

namespace oxygen::renderer {

  //! Mixin class that provides managed lifecycle functionality.
  /*!
   \tparam Base The base class to mix-in the managed lifecycle.
   \tparam CtorArgs The types of the arguments passed to the constructor. For
   each of these types arguments, a constructor argument needs to be passed, in
   the same order.

   This mixin adds the ability to initialize and shutdown an object. Two
   scenarios are possible:

   - The object is constructed with arguments; these arguments are stored and
     can be used during the `Initialize` stage. This is not the recommended way
     and should be only used for simple configuration parameters or data that is
     set only once, even if the object can be initialized and shutdown multiple
     times. Additional arguments can still be passed to the `Initialize` method.

   - The object is constructed without arguments; in this case, the `Initialize`
     method is the only place that receives initialization arguments. This is
     the recommended way to construct objects, as it allows for more flexibility
     and better separation of concerns.

   \note In both scenarios, the `Base` class should have `OnInitialize` and
   `OnShutdown` methods, which will be called during initialization and shutdown
   respectively. When called, `OnInitialize()` will receive the combined
   arguments of the constructor and the `Initialize()` method.
  */
  // ReSharper disable once CppClassCanBeFinal (mixin should never be made final)
  template <typename Base, typename... CtorArgs>
  class MixinManagedLifecycle : public Base
  {
  public:
    //! Forwarding constructor, for construction with arguments. \param
    /*!
     ctor_args The arguments passed to the constructor. The ones for which a
     type is specified in the template are stripped and stored for use by the
     `Initialize()` methods. The rest are forwarded to other mixins in the
     chain.
    */
    explicit MixinManagedLifecycle(CtorArgs... ctor_args)
      requires (sizeof...(CtorArgs) > 0)
    : ctor_args_(std::make_tuple(std::forward<CtorArgs>(ctor_args)...))
    {
    }

    //! Default constructor when CtorArgs is empty.
    MixinManagedLifecycle() requires (sizeof...(CtorArgs) == 0) = default;

    virtual ~MixinManagedLifecycle() noexcept
    {
      CHECK_F(!should_shutdown_, "{} object destroyed without calling Shutdown()", this->self().ObjectName());
    }

    OXYGEN_DEFAULT_COPYABLE(MixinManagedLifecycle);
    OXYGEN_DEFAULT_MOVABLE(MixinManagedLifecycle);

    //! Initialization.
    /*!
     \param init_args Configuration properties to customize the renderer and
     guide the initialization process.
     \throws std::runtime_error if the initialization fails.

     \note Initialization cannot happen again unless the object is shutdown
     first, which will be done automatically if not done prior to calling
     `Initialize()`.
    */
    template <typename... InitArgs>
    void Initialize(InitArgs... init_args)
    {
      // If already initialized, return.
      if (should_shutdown_) Shutdown();

      should_shutdown_ = true;
      LOG_F(INFO, "Initializing {}", this->self().ObjectName());
      try
      {
        if constexpr (sizeof...(CtorArgs) > 0)
        {
          auto combined_args = std::tuple_cat(ctor_args_, std::make_tuple(std::forward<InitArgs>(init_args)...));
          std::apply([this]<typename... Args>(Args&&... args) {
            this->self().OnInitialize(std::forward<Args>(args)...);
          }, combined_args);
        }
        else
        {
          this->self().OnInitialize(std::forward<InitArgs>(init_args)...);
        }
      }
      catch (const std::exception& e)
      {
        LOG_F(ERROR, "{} initialization error: {}", this->self().ObjectName(), e.what());
        Shutdown();
        throw std::runtime_error(std::string(this->self().ObjectName()) + " initialization error: " + std::string(e.what()));
      }
    }

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
      if (!should_shutdown_) return;

      LOG_F(INFO, "Shutting down {}", this->self().ObjectName());
      try
      {
        this->self().OnShutdown();
        should_shutdown_ = false;
      }
      catch (const std::exception& e)
      {
        should_shutdown_ = false;
        throw std::runtime_error(
          std::string(this->self().ObjectName()) + " shutdown incomplete: "
          + std::string(e.what()));
      }
    }

  private:
    bool should_shutdown_{ false };
    std::tuple<CtorArgs...> ctor_args_;
  };

}  // namespace oxygen::renderer
