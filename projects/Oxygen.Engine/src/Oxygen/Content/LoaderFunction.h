//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/FileStream.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Composition/TypeSystem.h>

namespace oxygen::content {

// Concept: LoaderFn must return a unique_ptr<XXX> where XXX is HasTypeInfo and
// accept (oxygen::serio::Reader<S>) for any S satisfying Stream

template <typename F, typename S>
concept LoaderFunctionForStream
  = oxygen::serio::Stream<S> && requires(F f, oxygen::serio::Reader<S> reader) {
      typename std::remove_cvref_t<decltype(*f(reader))>;
      requires IsTyped<std::remove_pointer_t<decltype(f(reader).get())>>;
      { f(reader) };
    };

// Main concept: true if F is callable with Reader<S> for FileStream<>

template <typename F>
concept LoaderFunction
  = LoaderFunctionForStream<F, oxygen::serio::FileStream<>>;

} // namespace oxygen::content
