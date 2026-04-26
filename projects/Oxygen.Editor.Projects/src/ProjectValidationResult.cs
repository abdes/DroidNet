// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Result of validating a project folder before workspace activation.
/// </summary>
public sealed record ProjectValidationResult
{
    /// <summary>
    ///     Gets the validation state.
    /// </summary>
    public required ProjectValidationState State { get; init; }

    /// <summary>
    ///     Gets the normalized project root path when it is known.
    /// </summary>
    public required string ProjectRoot { get; init; }

    /// <summary>
    ///     Gets the loaded project information when validation succeeds.
    /// </summary>
    public IProjectInfo? ProjectInfo { get; init; }

    /// <summary>
    ///     Gets the human-readable validation message.
    /// </summary>
    public string? Message { get; init; }

    /// <summary>
    ///     Gets the exception that caused the validation failure, when available.
    /// </summary>
    public Exception? Exception { get; init; }

    /// <summary>
    ///     Gets a value indicating whether validation succeeded.
    /// </summary>
    public bool IsValid => this.State == ProjectValidationState.Valid;

    /// <summary>
    ///     Creates a valid result.
    /// </summary>
    /// <param name="projectRoot">The normalized project root path.</param>
    /// <param name="projectInfo">The validated project info.</param>
    /// <returns>The validation result.</returns>
    public static ProjectValidationResult Valid(string projectRoot, IProjectInfo projectInfo)
        => new()
        {
            State = ProjectValidationState.Valid,
            ProjectRoot = projectRoot,
            ProjectInfo = projectInfo,
        };

    /// <summary>
    ///     Creates a failed result.
    /// </summary>
    /// <param name="state">The validation state.</param>
    /// <param name="projectRoot">The normalized project root path when known.</param>
    /// <param name="message">The validation message.</param>
    /// <param name="exception">The exception that caused the failure, when available.</param>
    /// <returns>The validation result.</returns>
    public static ProjectValidationResult Failure(
        ProjectValidationState state,
        string projectRoot,
        string message,
        Exception? exception = null)
        => new()
        {
            State = state,
            ProjectRoot = projectRoot,
            Message = message,
            Exception = exception,
        };
}
