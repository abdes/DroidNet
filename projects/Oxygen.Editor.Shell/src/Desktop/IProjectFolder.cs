// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Desktop;

/// <summary>
/// The folder of the project the editor works on, which desktop tools open as their workspace. The composition root
/// implements it in each project session.
/// </summary>
public interface IProjectFolder
{
    /// <summary>Gets the full path of the project's folder.</summary>
    public string Path { get; }
}
