// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>A model import record of a project, where it lives and the source it names.</summary>
/// <param name="RecordPath">The record's full path.</param>
/// <param name="Settings">The record's content.</param>
/// <param name="SourcePath">The full path of the model's primary source file.</param>
public sealed record ModelImportRecord(string RecordPath, ModelImportSettings Settings, string SourcePath)
{
    /// <summary>Gets the model's identity, which is where the record lives.</summary>
    public AssetPath Identity => this.Settings.Identity;
}
