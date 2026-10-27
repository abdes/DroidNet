// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.LooseCooked;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Expected bytes recorded by the native producer.</summary>
/// <param name="Size">The byte length.</param>
/// <param name="Sha256">The SHA-256 digest.</param>
/// <param name="Kind">The native file role, or null for keyed asset descriptors.</param>
public sealed record CookedInventoryMember(long Size, string Sha256, FileKind? Kind);
