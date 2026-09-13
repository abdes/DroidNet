// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Saved-source freshness, separate from unsaved edits and runtime availability.</summary>
public enum AssetCookFreshness
{
    /// <summary>No committed cook is known for this source.</summary>
    NeedsCooking,

    /// <summary>The saved dependency closure and verified output match the current producer.</summary>
    Current,

    /// <summary>A prior cook exists but saved inputs, dependencies or producer have changed.</summary>
    OutOfDate,

    /// <summary>The source or one of its required saved inputs is missing.</summary>
    MissingSource,

    /// <summary>Saved input cannot be read as supported authoring data.</summary>
    InvalidSource,

    /// <summary>Currentness could not be established, such as while native inputs are unavailable.</summary>
    Unknown,
}
