// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>Shows a problem in the window's Problems panel, so a part that reports problems needs no reference to it.</summary>
public interface IProblemReveal
{
    /// <summary>Selects a problem in the Problems panel and brings the panel forward.</summary>
    /// <param name="problem">The problem.</param>
    /// <returns><see langword="true"/> when the panel lists the problem and selected it.</returns>
    public Task<bool> RevealAsync(AssetProblem problem);

    /// <summary>Brings the Problems panel forward listing only the problems of one file, until the user shows them all.</summary>
    /// <param name="file">The file's full path, as its problems name it.</param>
    /// <returns><see langword="true"/> when the panel came forward.</returns>
    public Task<bool> ShowFileAsync(string file);
}
