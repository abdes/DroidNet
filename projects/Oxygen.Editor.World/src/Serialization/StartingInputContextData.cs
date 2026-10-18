// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// An input mapping context the scene activates when it starts.
/// </summary>
/// <param name="Context">The mapping context asset.</param>
/// <param name="Priority">The priority it is added with, or <see langword="null"/> for the context's own.</param>
public sealed record StartingInputContextData(Uri Context, int? Priority = null);
