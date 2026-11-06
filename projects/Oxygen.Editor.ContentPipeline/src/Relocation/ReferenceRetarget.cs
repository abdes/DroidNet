// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>A reference field rewritten to name another asset, while nothing moves.</summary>
/// <param name="File">The authored file that holds the reference.</param>
/// <param name="Property">The field's JSON path.</param>
/// <param name="Written">The text the field must hold now.</param>
/// <param name="Replacement">The text the field holds after.</param>
public sealed record ReferenceRetarget(FileSystemPath File, string Property, string Written, string Replacement)
{
    /// <summary>Gets the rewrite that undoes this one.</summary>
    /// <returns>The reverse rewrite.</returns>
    public ReferenceRetarget Reverse() => new(this.File, this.Property, this.Replacement, this.Written);
}
