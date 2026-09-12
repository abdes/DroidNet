// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Progress while checking the fixed artifact inventory.</summary>
/// <param name="ArtifactId">The artifact just checked.</param>
/// <param name="Checked">The number of completed checks, including failures.</param>
/// <param name="Total">The complete required artifact count.</param>
public sealed record ArtifactQualificationProgress(string ArtifactId, int Checked, int Total);
