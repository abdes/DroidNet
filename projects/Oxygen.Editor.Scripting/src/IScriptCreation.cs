// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scripting;

/// <summary>Creates a script where the user chooses, as New Script does, for the parts that put a new script to use.</summary>
public interface IScriptCreation
{
    /// <summary>Gets a value indicating whether a script can be created now; the part that asks the user is shown.</summary>
    public bool IsAvailable { get; }

    /// <summary>Asks for a name and folder and creates a script from the template.</summary>
    /// <returns>The new script's file, or <see langword="null"/> when the user cancelled or the creation failed.</returns>
    public Task<FileSystemPath?> CreateScriptAsync();
}
