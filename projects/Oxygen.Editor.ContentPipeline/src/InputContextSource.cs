// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>What the project says about a mapping context: whether it is one of the project's, and the priority its
/// input file declares.</summary>
/// <param name="IsProjectContext">Whether the context lives in the project, where an input file must declare it.</param>
/// <param name="DeclaredPriority">The priority the declaring input file gives it; null when no file declares it.</param>
public sealed record InputContextSource(bool IsProjectContext, int? DeclaredPriority)
{
    /// <summary>Gets what is known of a context outside the project, such as engine content.</summary>
    public static InputContextSource External { get; } = new(IsProjectContext: false, DeclaredPriority: null);

    /// <summary>Gets a value indicating whether the context is the project's and no input file declares it, so it is
    /// gone: it does not cook.</summary>
    public bool IsUndeclared => this.IsProjectContext && this.DeclaredPriority is null;

    /// <summary>Gets the priority the context starts with unless overridden.</summary>
    public int OwnPriority => this.DeclaredPriority ?? InputContextSources.DefaultPriority;
}
