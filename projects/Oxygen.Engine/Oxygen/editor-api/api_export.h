//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#ifndef OXYGEN_ENGINE_API_H
#define OXYGEN_ENGINE_API_H

#ifdef OXYGEN_ENGINE_STATIC_DEFINE
#define OXYGEN_ENGINE_API
#define OXYGEN_ENGINE_NO_EXPORT
#else
#ifndef OXYGEN_ENGINE_API
#ifdef OXYGEN_ENGINE_EXPORTS
/* We are building this library */
#define OXYGEN_ENGINE_API extern "C" __declspec(dllexport)
#else
/* We are using this library */
#define OXYGEN_ENGINE_API extern "C" __declspec(dllimport)
#endif
#endif

#ifndef OXYGEN_ENGINE_NO_EXPORT
#define OXYGEN_ENGINE_NO_EXPORT
#endif
#endif

#ifndef OXYGEN_ENGINE_DEPRECATED
#define OXYGEN_ENGINE_DEPRECATED __declspec(deprecated)
#endif

#ifndef OXYGEN_ENGINE_DEPRECATED_EXPORT
#define OXYGEN_ENGINE_DEPRECATED_EXPORT                                        \
  OXYGEN_ENGINE_API OXYGEN_ENGINE_DEPRECATED
#endif

#ifndef OXYGEN_ENGINE_DEPRECATED_NO_EXPORT
#define OXYGEN_ENGINE_DEPRECATED_NO_EXPORT                                     \
  OXYGEN_ENGINE_NO_EXPORT OXYGEN_ENGINE_DEPRECATED
#endif

#if 0 /* DEFINE_NO_DEPRECATED */
#ifndef OXYGEN_ENGINE_NO_DEPRECATED
#define OXYGEN_ENGINE_NO_DEPRECATED
#endif
#endif

#endif /* OXYGEN_ENGINE_API_H */
