// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.CodeAnalysis;

namespace DroidNet.Resources.Generator;

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
    public const string Category = "DroidNet.Resources.Generator";

    /// <summary>
    ///     A name for the generator, used in GeneratedCode attributes or diagnostics if needed.
    /// </summary>
    public const string GeneratorName = "DroidNet.Resources.Generator";

    /// <summary>
    ///     Diagnostic indicating the generator has started processing the assembly.
    /// </summary>
    public static readonly DiagnosticDescriptor GeneratorStarted = new(
        id: "DROIDRES001",
        title: "Generator Started",
        messageFormat: "DroidNet.Resources.Generator started processing assembly '{0}'",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true);

    /// <summary>
    ///     Diagnostic indicating the generator has completed processing the assembly.
    /// </summary>
    public static readonly DiagnosticDescriptor GenerationComplete = new(
        id: "DROIDRES002",
        title: "Generation Complete",
        messageFormat: "DroidNet.Resources.Generator completed for assembly '{0}'. Generated files: {1}.",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true);

    /// <summary>
    ///     Diagnostic when the generator fails with an exception.
    /// </summary>
    public static readonly DiagnosticDescriptor GeneratorFailure = new(
        id: "DNRESGEN001",
        title: "Resources generator failure",
        messageFormat: "DroidNet.Resources generator failed: {0}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Error,
        isEnabledByDefault: true,
        description: "An unhandled exception occurred during source generation. This diagnostic indicates a generator failure and the message contains the exception message.",
        helpLinkUri: "https://github.com/abdes/DroidNet/blob/master/projects/Resources.Generator/README.md#diagnostics");

    /// <summary>
    ///     Diagnostic indicating whether the runtime ResourceExtensions type was located.
    /// </summary>
    public static readonly DiagnosticDescriptor FoundResourceExtensions = new(
        id: "DNRESGEN002",
        title: "ResourceExtensions runtime type presence",
        messageFormat: "ResourceExtensions type '{0}' {1} in compilation",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true,
        description: "Indicates whether the runtime ResourceExtensions type was found in the current compilation.");

    /// <summary>
    ///     Diagnostic indicating we found an existing Localized type in the target assembly.
    /// </summary>
    public static readonly DiagnosticDescriptor FoundExistingType = new(
        id: "DNRESGEN003",
        title: "Existing Localized type found",
        messageFormat: "Found existing type '{0}', isUserDefined={1}, declarations={2}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true,
        description: "Indicates a Localized type already exists and the generator may skip generation to avoid collision.");

    /// <summary>
    ///     Diagnostic indicating the generator is skipping generation for user-defined type.
    /// </summary>
    public static readonly DiagnosticDescriptor SkippingUserDefined = new(
        id: "DNRESGEN004",
        title: "Skipping generation due to user-defined type",
        messageFormat: "Skipping generation - user-defined type exists: {0}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true,
        description: "When a user-defined type conflicts with the generated type, the generator will skip generation.");

    /// <summary>
    ///     Diagnostic indicating the generator is regenerating a previously generated type.
    /// </summary>
    public static readonly DiagnosticDescriptor Regenerating = new(
        id: "DNRESGEN005",
        title: "Regenerating helper type",
        messageFormat: "Regenerating for clean build: {0}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true,
        description: "Indicates the generator detected a previously generated type and is regenerating for a clean build.");

    /// <summary>
    ///     Diagnostic indicating no existing Localized helper was found and generation will proceed.
    /// </summary>
    public static readonly DiagnosticDescriptor NoExisting = new(
        id: "DNRESGEN006",
        title: "No existing localization helper found",
        messageFormat: "No existing type found - generating: {0}",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true,
        description: "Indicates a Localized helper type does not yet exist, and the generator will create one.");

    /// <summary>
    ///     Diagnostic indicating the DI framework (DryIoc) is not present; DI extension will not be generated.
    /// </summary>
    public static readonly DiagnosticDescriptor DIFrameworkMissing = new(
        id: "DNRESGEN007",
        title: "DI framework missing",
        messageFormat: "DryIoc IContainer not found in project references. DI extension will not be emitted.",
        category: Category,
        defaultSeverity: DiagnosticSeverity.Info,
        isEnabledByDefault: true,
        description: "Indicates the DryIoc DI types required for DI extension generation are not available in the project references.");

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
        var diag = Diagnostic.Create(descriptor, loc, messageArgs ?? []);
        context.ReportDiagnostic(diag);
    }
}
