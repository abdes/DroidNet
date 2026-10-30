// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Text.Json;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Exception thrown when a project manifest declares an unsupported schema version.
/// </summary>
/// <param name="actualVersion">The schema version found in the manifest.</param>
/// <param name="expectedVersion">The supported schema version.</param>
[SuppressMessage(
    "Design",
    "CA1032:Implement standard exception constructors",
    Justification = "The manifest schema versions are required to build a useful validation result.")]
public sealed class UnsupportedProjectSchemaException(int actualVersion, int expectedVersion)
    : JsonException(string.Create(CultureInfo.InvariantCulture, $"Unsupported Project.oxy schema version {actualVersion}; expected {expectedVersion}."))
{
    /// <summary>
    ///     Gets the schema version found in the manifest.
    /// </summary>
    public int ActualVersion { get; } = actualVersion;

    /// <summary>
    ///     Gets the supported schema version.
    /// </summary>
    public int ExpectedVersion { get; } = expectedVersion;
}
