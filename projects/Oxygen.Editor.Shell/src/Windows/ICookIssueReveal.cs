// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Shell.Windows;

/// <summary>
/// Shows what a cook issue names in the open project, so a part that lists a cook's problems, such as the Problems
/// panel, needs no reference to the part that cooks.
/// </summary>
public interface ICookIssueReveal
{
    /// <summary>Opens and focuses what a cook issue names: a scene property, a script slot's node, or an instance's kept changes.</summary>
    /// <param name="issue">The issue, with its structured target.</param>
    /// <returns>Whether the target was accepted for navigation.</returns>
    public Task<bool> RevealAsync(DiagnosticRecord issue);
}
