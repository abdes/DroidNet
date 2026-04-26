// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Result of creating a project from a template payload.
/// </summary>
public sealed record ProjectCreationResult
{
    /// <summary>
    ///     Gets a value indicating whether project creation succeeded.
    /// </summary>
    public required bool Succeeded { get; init; }

    /// <summary>
    ///     Gets the project root path.
    /// </summary>
    public required string ProjectRoot { get; init; }

    /// <summary>
    ///     Gets the created project info when creation succeeds.
    /// </summary>
    public IProjectInfo? ProjectInfo { get; init; }

    /// <summary>
    ///     Gets the validation result for the created project when validation ran.
    /// </summary>
    public ProjectValidationResult? Validation { get; init; }

    /// <summary>
    ///     Gets the failure message when creation fails.
    /// </summary>
    public string? Message { get; init; }

    /// <summary>
    ///     Gets the exception that caused creation to fail, when available.
    /// </summary>
    public Exception? Exception { get; init; }

    /// <summary>
    ///     Creates a successful project creation result.
    /// </summary>
    /// <param name="projectRoot">The project root.</param>
    /// <param name="projectInfo">The created project info.</param>
    /// <param name="validation">The project validation result.</param>
    /// <returns>The project creation result.</returns>
    public static ProjectCreationResult Success(
        string projectRoot,
        IProjectInfo projectInfo,
        ProjectValidationResult validation)
        => new()
        {
            Succeeded = true,
            ProjectRoot = projectRoot,
            ProjectInfo = projectInfo,
            Validation = validation,
        };

    /// <summary>
    ///     Creates a failed project creation result.
    /// </summary>
    /// <param name="projectRoot">The project root, when known.</param>
    /// <param name="message">The failure message.</param>
    /// <param name="validation">The validation result, when available.</param>
    /// <param name="exception">The exception that caused the failure, when available.</param>
    /// <returns>The project creation result.</returns>
    public static ProjectCreationResult Failure(
        string projectRoot,
        string message,
        ProjectValidationResult? validation = null,
        Exception? exception = null)
        => new()
        {
            Succeeded = false,
            ProjectRoot = projectRoot,
            Message = message,
            Validation = validation,
            Exception = exception,
        };
}
