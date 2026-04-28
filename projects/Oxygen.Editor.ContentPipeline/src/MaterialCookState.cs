// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Material cook states shown by editor material workflows.
/// </summary>
public enum MaterialCookState
{
    /// <summary>
    /// The material has no cooked output.
    /// </summary>
    NotCooked,

    /// <summary>
    /// The material cooked successfully and the cooked output is current.
    /// </summary>
    Cooked,

    /// <summary>
    /// The material has cooked output, but its source descriptor is newer.
    /// </summary>
    Stale,

    /// <summary>
    /// The cook failed.
    /// </summary>
    Failed,

    /// <summary>
    /// The request was rejected before cook execution.
    /// </summary>
    Rejected,

    /// <summary>
    /// The material source is unsupported by the ED-M05 scalar material slice.
    /// </summary>
    Unsupported,
}
