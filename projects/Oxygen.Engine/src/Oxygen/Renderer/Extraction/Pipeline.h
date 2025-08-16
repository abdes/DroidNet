//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <tuple>
#include <type_traits>

#include <Oxygen/Renderer/Extraction/Extractors.h>

namespace oxygen::engine::extraction {

namespace detail {
  template <class> inline constexpr bool always_false = false;

  template <std::size_t I = 0, typename Tuple>
  void execute_tuple(
    Tuple const& t, WorkItem& item, ExtractorContext const& ctx, Collector out)
  {
    if constexpr (I < std::tuple_size_v<Tuple>) {
      auto const& fn = std::get<I>(t);
      using FnT = std::decay_t<decltype(fn)>;

      if constexpr (FilterFn<FnT>) {
        if (!fn(item, ctx)) {
          item.dropped = true;
          return; // stop pipeline on drop
        }
      } else if constexpr (ProducerFn<FnT>) {
        fn(item, ctx, out);
        // Producer may choose to not stop; we continue
      } else if constexpr (UpdaterFn<FnT>) {
        fn(item, ctx);
      } else {
        static_assert(always_false<FnT>, "Unsupported extractor function type");
      }

      if (item.dropped)
        return;
      execute_tuple<I + 1, Tuple>(t, item, ctx, out);
    }
  }

} // namespace detail

template <typename... Extractors> struct Pipeline {
  std::tuple<Extractors...> extractors;

  constexpr Pipeline(Extractors... e)
    : extractors(std::move(e)...)
  {
  }

  void operator()(
    WorkItem& item, ExtractorContext const& ctx, Collector out) const
  {
    detail::execute_tuple(extractors, item, ctx, out);
    // if not dropped and nothing emitted, default emit behavior can be
    // performed
  }
};

} // namespace oxygen::engine::extraction
