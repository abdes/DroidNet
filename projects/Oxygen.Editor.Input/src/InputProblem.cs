// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// A problem of an input source, with one of the <see cref="InputSourceCodes"/>; the source does not cook until it is
/// fixed.
/// </summary>
public sealed record InputProblem : AssetProblem
{
    /// <summary>Initializes a new instance of the <see cref="InputProblem"/> class.</summary>
    /// <param name="problem">The problem as the format or the input rules report it, with its file.</param>
    /// <param name="container">The identity of the input source that has the problem.</param>
    public InputProblem(AssetProblem problem, AssetPath container)
        : base(problem ?? throw new ArgumentNullException(nameof(problem)))
    {
        ArgumentNullException.ThrowIfNull(container);
        this.Container = container;
    }

    /// <summary>Gets the identity of the input source that has the problem, such as <c>/Content/Input/Player.input</c>.</summary>
    public AssetPath Container { get; }
}
