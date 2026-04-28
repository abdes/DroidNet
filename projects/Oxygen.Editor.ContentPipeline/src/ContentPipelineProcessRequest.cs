// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Process invocation requested by the bounded content-pipeline tool adapter.
/// </summary>
/// <param name="ExecutablePath">The executable path.</param>
/// <param name="Arguments">The process arguments, already split into tokens.</param>
/// <param name="WorkingDirectory">The working directory.</param>
public sealed record ContentPipelineProcessRequest(
    string ExecutablePath,
    IReadOnlyList<string> Arguments,
    string WorkingDirectory);
