// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>An unresolved reference, as the Problems pane lists it.</summary>
public sealed record UnresolvedReferenceProblem : AssetProblem
{
    /// <summary>Initializes a new instance of the <see cref="UnresolvedReferenceProblem"/> class.</summary>
    /// <param name="reference">The unresolved reference.</param>
    public UnresolvedReferenceProblem(UnresolvedReference reference)
        : base(
            DiagnosticSeverity.Warning,
            (reference ?? throw new ArgumentNullException(nameof(reference))).Code,
            reference.Resolved is { } resolved && !string.Equals(resolved, reference.Written, StringComparison.Ordinal)
                ? $"'{reference.File.Name}' references '{reference.Written}' at {reference.Property}, which is '{resolved}' and does not resolve in this project."
                : $"'{reference.File.Name}' references '{reference.Written}' at {reference.Property}, which does not resolve in this project.")
    {
        this.Reference = reference;
        this.File = reference.File.FullPath;
    }

    /// <summary>Gets the unresolved reference.</summary>
    public UnresolvedReference Reference { get; }
}
