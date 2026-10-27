// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>One observed mismatch in the protected root.</summary>
/// <param name="RelativePath">The affected relative path.</param>
/// <param name="Reason">The native schema's failure classification.</param>
public sealed record CookedInventoryIssue(string RelativePath, string Reason);
