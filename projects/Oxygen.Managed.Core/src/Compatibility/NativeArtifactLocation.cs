// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>A required artifact resolved by the host before native loading.</summary>
/// <param name="Id">The artifact's portable identity in the build receipt.</param>
/// <param name="FullPath">Its absolute path in the current installation.</param>
/// <param name="SchemaId">The schema identifier required by the consumer, when applicable.</param>
public sealed record NativeArtifactLocation(string Id, string FullPath, string? SchemaId = null);
