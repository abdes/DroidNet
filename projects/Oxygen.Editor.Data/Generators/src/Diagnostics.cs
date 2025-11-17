// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.CodeAnalysis;

namespace Oxygen.Editor.Data.Generators;

/// <summary>
///     Centralized diagnostics for the DroidNet.Resources generator.
/// </summary>
// StyleCop and analyzers are noisy for descriptor-only files; turn off a couple of rules for this file.
[SuppressMessage("RoslynDiagnostics", "RS2008", Justification = "This project produces a runtime source generator (not a shipped analyzer). Release-tracking is only required for published analyzer packages; suppress RS2008 to avoid noisy warnings in generator-only projects.")]
internal static class Diagnostics
{
    /// <summary>
    ///     Diagnostic category used for all generator diagnostics.
    /// </summary>
    public const string Category = "Oxygen.Editor.Data.Generator";

    /// <summary>
    ///     A name for the generator, used in GeneratedCode attributes or diagnostics if needed.
    /// </summary>
    public const string GeneratorName = "Oxygen.Editor.Data.Generator";

    /// <summary>
    ///     Diagnostic indicating the generator has started processing the assembly.
    /// </summary>
    public static readonly DiagnosticDescriptor GeneratorStarted = new(
        id: "OXGNPG000",
        title: "Generator Started",
        messageFormat: "Oxygen.Editor.Data.Generator started processing class '{0}'",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic for generic generator failures (catch-all).
    /// </summary>
    public static readonly DiagnosticDescriptor GeneratorFailure = new(
        id: "OXGNPG999",
        title: "Generator Failure",
        messageFormat: "Generator failure: {0}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic emitted when a ModuleSettings class is not declared partial.
    /// </summary>
    public static readonly DiagnosticDescriptor ClassMustBePartial = new(
        id: "OXGNPG001",
        title: "ModuleSettings class must be declared partial",
        messageFormat: "ModuleSettings class '{0}' must be declared partial",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic emitted when ModuleName constant is missing.
    /// </summary>
    public static readonly DiagnosticDescriptor ModuleNameMissing = new(
        id: "OXGNPG002",
        title: "ModuleName constant missing",
        messageFormat: "Class '{0}' must declare a 'private new const string ModuleName' constant",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic emitted when ModuleName isn't a const string.
    /// </summary>
    public static readonly DiagnosticDescriptor ModuleNameNotConst = new(
        id: "OXGNPG003",
        title: "ModuleName must be const",
        messageFormat: "ModuleName in class '{0}' must be a compile-time constant string",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic warning for non-public properties marked with Persisted.
    /// </summary>
    public static readonly DiagnosticDescriptor PersistedPropertyNotPublic = new(
        id: "OXGNPG101",
        title: "[Persisted] property must be public",
        messageFormat: "Property '{0}' in class '{1}' is marked with [Persisted] but is not public",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Warning,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic warning for properties annotated with Persisted but lacking a setter.
    /// </summary>
    public static readonly DiagnosticDescriptor PersistedPropertyMissingSetter = new(
        id: "OXGNPG102",
        title: "[Persisted] property must have a setter",
        messageFormat: "Property '{0}' in class '{1}' is marked [Persisted] but has no setter",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Warning,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic error for properties that don't appear serializable.
    /// </summary>
    public static readonly DiagnosticDescriptor PersistedPropertyNotSerializable = new(
        id: "OXGNPG103",
        title: "Property type must be serializable",
        messageFormat: "Property '{0}' in class '{1}' has a type that appears not JSON-serializable",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic error for properties on nested ModuleSettings classes decorated with Persisted.
    /// </summary>
    public static readonly DiagnosticDescriptor PersistedPropertyNested = new(
        id: "OXGNPG104",
        title: "Nested ModuleSettings are not supported",
        messageFormat: "Property '{0}' in nested class '{1}' is marked with [Persisted] but nested ModuleSettings classes are not supported",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    /// <summary>
    /// Diagnostic warning when a ValidationAttribute has no meaningful parameters.
    /// </summary>
    public static readonly DiagnosticDescriptor ValidatorNoEffect = new(
        id: "OXGNPG201",
        title: "Validator has no constraints",
        messageFormat: "Validation attribute on property '{0}' in class '{1}' has no explicit constructor parameters or named properties and has no effect",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Warning,
        isEnabledByDefault: true);

    /// <summary>
    ///     Report a diagnostic using a centralized descriptor and optional location and message arguments.
    /// </summary>
    /// <param name="context">The <see cref="SourceProductionContext"/> used to report the diagnostic.</param>
    /// <param name="descriptor">The <see cref="DiagnosticDescriptor"/> that describes the diagnostic to report.</param>
    /// <param name="location">An optional <see cref="Location"/> indicating where to report the diagnostic in source.</param>
    /// <param name="messageArgs">Optional message-format arguments for the descriptor.</param>
    public static void Report(SourceProductionContext context, DiagnosticDescriptor descriptor, Location? location = null, params object?[] messageArgs)
    {
        if (context.CancellationToken.IsCancellationRequested)
        {
            return;
        }

        var loc = location ?? Location.None;
        var args = messageArgs ?? [];
        var diag = Diagnostic.Create(descriptor, loc, args);
        context.ReportDiagnostic(diag);
    }
}
