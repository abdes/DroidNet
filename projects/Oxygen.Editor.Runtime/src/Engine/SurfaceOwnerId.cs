// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Identifies the owner of leased viewport surfaces, such as the scene document whose editor shows them.
/// </summary>
/// <param name="Value">The owner's identity.</param>
public readonly record struct SurfaceOwnerId(Guid Value);
