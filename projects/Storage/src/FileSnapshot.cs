// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace DroidNet.Storage;

/// <summary>Complete file bytes and the identity captured from those same bytes.</summary>
/// <param name="Content">The owned immutable bytes.</param>
/// <param name="Version">The content identity.</param>
public sealed record FileSnapshot(ImmutableArray<byte> Content, FileVersion Version);
