// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Projects.Mounts;

/// <summary>A diagnostic found while resolving a project's mount table.</summary>
/// <param name="Code">The code, one of <see cref="MountCodes"/>.</param>
/// <param name="Message">What is wrong, naming the mount and the folder.</param>
/// <param name="Mount">The mount it is about, if any.</param>
/// <param name="File">The file that declares or binds it, if any.</param>
public sealed record MountFinding(string Code, string Message, string? Mount = null, FileSystemPath? File = null)
{
    /// <summary>Gets a value indicating whether the finding is an error, which makes the table invalid.</summary>
    public bool IsError => !MountCodes.IsWarning(this.Code);
}
