// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Cooking;

/// <summary>Execution states of one scoped cooking operation.</summary>
public enum CookRunState
{
    /// <summary>Waiting for the shared writer.</summary>
    Queued,

    /// <summary>Waiting for explicitly authorized document saves.</summary>
    NeedsSave,

    /// <summary>Resolving and capturing saved inputs.</summary>
    Preparing,

    /// <summary>Producing derived content.</summary>
    Cooking,

    /// <summary>Checking produced content.</summary>
    Validating,

    /// <summary>Installing validated content and updating runtime consumers.</summary>
    Publishing,

    /// <summary>Waiting for owned work to reach a safe stop.</summary>
    Cancelling,

    /// <summary>The cook completed successfully.</summary>
    Succeeded,

    /// <summary>The cook completed with actionable warnings.</summary>
    SucceededWithWarnings,

    /// <summary>The request reused current validated output.</summary>
    UpToDate,

    /// <summary>The operation failed.</summary>
    Failed,

    /// <summary>Owned work has stopped safely.</summary>
    Cancelled,
}
