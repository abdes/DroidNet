//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/StringUtils.h"

namespace oxygen {

  //! Mixin class that provides a name to the object which can be used for
  //! logging and debugging.
  // ReSharper disable once CppClassCanBeFinal (mixin should never be made final)
  template <typename Base, string_utils::AnyStringType Name>
  class MixinNamed : public Base
  {
  public:
    //! Forwarding constructor, which expects the name of the object as the
    //! first argument
    /*!
     \param class_name Name of the object, which will be converted to UTF-8.
     \param args Arguments to forward to other mixins in the chain.
    */
    template <typename... Args>
    constexpr explicit MixinNamed(Name class_name, Args &&...args)
      : Base(std::forward<Args>(args)...)
    {
      string_utils::WideToUtf8(class_name, object_name_);
    }

    virtual ~MixinNamed() noexcept = default;

    OXYGEN_DEFAULT_COPYABLE(MixinNamed);
    OXYGEN_DEFAULT_MOVABLE(MixinNamed);

    //! Returns the name of the object as a UTF-8 string. Use
    //! `string_utils::Utf8ToWide` to convert it to a wide string if needed.
    [[nodiscard]] auto ObjectName() const -> const std::string& { return object_name_; }

  private:
    std::string object_name_;
  };

}  // namespace oxygen
