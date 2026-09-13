// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.World.Inspection;

/// <summary>A file belonging to an inspected physical root.</summary>
/// <param name="RootName">The owning mount.</param>
/// <param name="RootPath">The physical root.</param>
/// <param name="File">The native file entry.</param>
public sealed record InspectionFileRow(string RootName, string RootPath, CookedFileEntry File)
{
    /// <summary>Gets the root-relative file path.</summary>
    public string Name => this.RootName + "/" + this.File.RelativePath;

    /// <summary>Gets the exact file size.</summary>
    public string Size => string.Create(CultureInfo.CurrentCulture, $"{this.File.Size:N0} bytes");

    /// <summary>Gets the copyable physical path.</summary>
    public string FullPath => Path.Combine(this.RootPath, this.File.RelativePath);
}
