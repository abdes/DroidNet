// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>How an ImportTool run ended, by its exit code.</summary>
public enum NativeRunOutcome
{
    /// <summary>Every job succeeded.</summary>
    Succeeded = 0,

    /// <summary>The tool refused the request before any job ran.</summary>
    Refused = 1,

    /// <summary>A job failed; its report says why.</summary>
    Failed = 2,

    /// <summary>The run stopped on a request from outside: the root stays at its last commit.</summary>
    Stopped = 3,

    /// <summary>The tool itself failed: a defect, not a problem of the content.</summary>
    ToolDefect = 4,
}
