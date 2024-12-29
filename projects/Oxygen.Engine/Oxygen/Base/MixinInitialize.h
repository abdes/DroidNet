#pragma once
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <stdexcept>
#include <tuple>
#include <utility>

#include "Oxygen/Base/Logging.h"
#include "Oxygen/Base/Macros.h"

namespace oxygen {

  //! Mixin class that adds explicit Initialize() to a class.
  /*!
   \tparam Base The base class to mix-in the managed lifecycle.
   \tparam CtorArgs The types of the arguments passed to the constructor. For
   each of these types arguments, a constructor argument needs to be passed, in
   the same order.

   This mixin adds the ability to explicitly initialize an object. Two scenarios
   are possible:

   - The object is constructed with arguments; these arguments are stored and
     can be used during the `Initialize` stage. This is not the recommended way
     and should be only used for simple configuration parameters or data that is
     set only once, even if the object can be initialized and shutdown multiple
     times. Additional arguments can still be passed to the `Initialize` method.

   - The object is constructed without arguments; in this case, the `Initialize`
     method is the only place that receives initialization arguments. This is
     the recommended way to construct objects, as it allows for more flexibility
     and better separation of concerns.

   \note In both scenarios, the `Base` class should have `OnInitialize` method,
   which will be called during initialization. When called, `OnInitialize()`
   will receive the combined arguments of the constructor and the `Initialize()`
   method.

   \note Requires the `MixinNamed` to be mixed in.
  */
  // ReSharper disable once CppClassCanBeFinal (mixin should never be made final)
  template <typename Base, typename... CtorArgs>
  class MixinInitialize : public Base
  {
  public:
    //! Forwarding constructor, for construction with arguments. \param
    /*!
     \param ctor_args The arguments passed to the constructor. The ones for which a
     type is specified in the template are stripped and stored for use by the
     `Initialize()` methods. The rest are forwarded to other mixins in the
     chain.
    */
    explicit MixinInitialize(CtorArgs... ctor_args)
      requires (sizeof...(CtorArgs) > 0)
    : ctor_args_(std::make_tuple(std::forward<CtorArgs>(ctor_args)...))
    {
    }

    //! Default constructor when CtorArgs is empty.
    MixinInitialize() requires (sizeof...(CtorArgs) == 0) = default;

    virtual ~MixinInitialize() noexcept
    {
      if (is_initialized_)
      {
        DLOG_F(WARNING, "{} object destroyed without calling Shutdown()", debug_object_name_);
      }
    }

    OXYGEN_DEFAULT_COPYABLE(MixinInitialize);
    OXYGEN_DEFAULT_MOVABLE(MixinInitialize);

    //! Initialization.
    /*!
     \param init_args Configuration properties to customize the the
     initialization process.
     \throws std::runtime_error if the initialization fails.
    */
    template <typename... InitArgs>
    void Initialize(InitArgs... init_args)
    {
      debug_object_name_ = this->self().ObjectName();
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
        is_initialized_ = true;
      }
      catch (const std::exception& e)
      {
        LOG_F(ERROR, "{} initialization error: {}", this->self().ObjectName(), e.what());
        throw std::runtime_error(std::string(this->self().ObjectName()) + " initialization error: " + std::string(e.what()));
      }
    }

    //! Checks if the object has been initialized.
    [[nodiscard]] auto IsInitialized() const -> bool { return is_initialized_; }

    //! Marks the object as uninitialized.
    void IsInitialized(const bool state) { is_initialized_ = state; }

  private:
    std::tuple<CtorArgs...> ctor_args_;

    bool is_initialized_{ false };
    // Store the object name for tracking unreleased destroyed objects.
    std::string debug_object_name_{ };
  };

}  // namespace oxygen::renderer
