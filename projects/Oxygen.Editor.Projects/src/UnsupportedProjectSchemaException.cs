// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Exception thrown when a project manifest declares an unsupported schema version.
/// </summary>
[SuppressMessage(
    "Design",
    "CA1032:Implement standard exception constructors",
    Justification = "The manifest schema versions are required to build a useful validation result.")]
public sealed class UnsupportedProjectSchemaException : JsonException
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="UnsupportedProjectSchemaException"/> class.
    /// </summary>
    /// <param name="actualVersion">The schema version found in the manifest.</param>
    /// <param name="expectedVersion">The supported schema version.</param>
    public UnsupportedProjectSchemaException(int actualVersion, int expectedVersion)
        : base($"Unsupported Project.oxy schema version {actualVersion}; expected {expectedVersion}.")
    {
        this.ActualVersion = actualVersion;
        this.ExpectedVersion = expectedVersion;
    }

    /// <summary>
    ///     Gets the schema version found in the manifest.
    /// </summary>
    public int ActualVersion { get; }

    /// <summary>
    ///     Gets the supported schema version.
    /// </summary>
    public int ExpectedVersion { get; }
}
