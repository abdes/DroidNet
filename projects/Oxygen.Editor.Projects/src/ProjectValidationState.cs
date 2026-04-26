// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Canonical project validation states used before workspace activation.
/// </summary>
public enum ProjectValidationState
{
    /// <summary>
    ///     The project is valid and can be activated.
    /// </summary>
    Valid,

    /// <summary>
    ///     The requested project folder does not exist.
    /// </summary>
    Missing,

    /// <summary>
    ///     The requested folder exists but does not contain a project manifest.
    /// </summary>
    NotAProject,

    /// <summary>
    ///     The project manifest is present but malformed.
    /// </summary>
    InvalidManifest,

    /// <summary>
    ///     The project manifest declares a schema version this editor does not support.
    /// </summary>
    UnsupportedVersion,

    /// <summary>
    ///     The project cannot be inspected because storage denied or failed access.
    /// </summary>
    Inaccessible,

    /// <summary>
    ///     The project manifest is structurally valid but declares missing or invalid content roots.
    /// </summary>
    InvalidContentRoots,
}
