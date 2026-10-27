// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>A problem a cook reported, with the diagnostic it comes from, whose Go To Property leads to its node.</summary>
public sealed record CookProblem : AssetProblem
{
    /// <summary>Initializes a new instance of the <see cref="CookProblem"/> class.</summary>
    /// <param name="issue">The cook's diagnostic.</param>
    public CookProblem(DiagnosticRecord issue)
        : base(issue?.Severity ?? throw new ArgumentNullException(nameof(issue)), issue.Code, issue.Message)
    {
        this.Issue = issue;
        this.File = issue.AffectedPath;
        this.Explanation = issue.TechnicalMessage;
    }

    /// <summary>Gets the cook's diagnostic.</summary>
    public DiagnosticRecord Issue { get; }
}
