// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.CodeAnalysis;

namespace Oxygen.Editor.Schemas.Generators;

/// <summary>The errors of the scene components generator. Each one but the configuration errors points into the schema file.</summary>
[SuppressMessage("RoslynDiagnostics", "RS2008", Justification = "A build-time generator of this repository, not a shipped analyzer package.")]
internal static class SceneComponentsDiagnostics
{
    private const string Category = "Oxygen.Editor.Schemas.Generator";

    /// <summary>The project asks for an output the generator does not produce, or for none.</summary>
    public static readonly DiagnosticDescriptor UnknownOutput = Error(
        "OXGNSC001",
        "Unknown scene components output",
        "SceneComponentsOutput '{0}' is not one of {1}");

    /// <summary>The project has no schema file, or more than one.</summary>
    public static readonly DiagnosticDescriptor SchemaFileCount = Error(
        "OXGNSC002",
        "Scene components schema missing",
        "The scene components outputs need exactly one SceneComponents.yaml additional file; found {0}");

    /// <summary>The schema file is not well-formed YAML.</summary>
    public static readonly DiagnosticDescriptor InvalidYaml = Error(
        "OXGNSC003",
        "Scene components schema is not valid YAML",
        "{0}");

    /// <summary>A node of the schema has the wrong shape: a missing or unknown key, or a value of the wrong kind.</summary>
    public static readonly DiagnosticDescriptor InvalidShape = Error(
        "OXGNSC004",
        "Scene components schema has an invalid entry",
        "{0}");

    /// <summary>A component names a field set, or a property a field, that does not exist.</summary>
    public static readonly DiagnosticDescriptor UnknownField = Error(
        "OXGNSC005",
        "Unknown field set or field",
        "{0}: {1}");

    /// <summary>A property's value type or enumeration has no editor type.</summary>
    public static readonly DiagnosticDescriptor UnknownType = Error(
        "OXGNSC006",
        "Unknown value type",
        "{0}: '{1}' has no editor type");

    /// <summary>A property's fields do not match its value type, or a property of a component without a wire has fields.</summary>
    public static readonly DiagnosticDescriptor FieldMismatch = Error(
        "OXGNSC007",
        "Fields do not match the value",
        "{0}: {1}");

    /// <summary>A property names an encoding the editor does not implement.</summary>
    public static readonly DiagnosticDescriptor UnknownEncoding = Error(
        "OXGNSC008",
        "Unknown encoding",
        "{0}: the editor implements no encoding '{1}'");

    private static DiagnosticDescriptor Error(string id, string title, string message)
        => new(id, title, message, Category, DiagnosticSeverity.Error, isEnabledByDefault: true);
}
