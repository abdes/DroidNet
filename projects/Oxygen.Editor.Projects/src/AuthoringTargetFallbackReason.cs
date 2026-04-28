// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
///     Reasons an authoring target resolver fell back to the asset-kind default folder.
/// </summary>
public enum AuthoringTargetFallbackReason
{
    /// <summary>
    ///     No selected folder was provided.
    /// </summary>
    NoSelection,

    /// <summary>
    ///     The selected folder is not under an authored mount.
    /// </summary>
    NonAuthoringSelection,

    /// <summary>
    ///     The selected authored folder belongs to a different asset kind.
    /// </summary>
    KindMismatch,

    /// <summary>
    ///     The selected local mount is not declared by the active project.
    /// </summary>
    UnknownLocalMount,
}
