// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Activation;

/// <summary>
/// Project activation workflow kind.
/// </summary>
public enum ProjectActivationMode
{
    /// <summary>
    /// Open an existing project folder.
    /// </summary>
    OpenExisting,

    /// <summary>
    /// Create a project from a template and then open it.
    /// </summary>
    CreateFromTemplate,
}
