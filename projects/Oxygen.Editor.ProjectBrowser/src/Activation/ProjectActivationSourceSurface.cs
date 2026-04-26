// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Activation;

/// <summary>
/// Project Browser surface that initiated an activation request.
/// </summary>
public enum ProjectActivationSourceSurface
{
    /// <summary>
    /// Project Browser home surface.
    /// </summary>
    Home,

    /// <summary>
    /// Explicit open-project surface.
    /// </summary>
    Open,

    /// <summary>
    /// New-project surface.
    /// </summary>
    New,
}
