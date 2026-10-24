// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Materials.Documents;

/// <summary>
///     Checks material sources against the engine's material schema, and an edited source against the editor's overlay
///     too, so the editor never writes a material the cooker would reject.
/// </summary>
/// <remarks>
///     The schemas are read from the editor's output once. A source is always checked against the engine schema that
///     <see cref="MaterialSourceWriter"/> embeds, so a source it would refuse to write is refused here, with its reason,
///     even when the schemas cannot be read.
/// </remarks>
public sealed partial class MaterialValidation
{
    private readonly ILogger logger;
    private readonly Lazy<MaterialSchemaValidator?> validator;

    /// <summary>
    ///     Initializes a new instance of the <see cref="MaterialValidation"/> class.
    /// </summary>
    /// <param name="loggerFactory">The logger factory, if any.</param>
    public MaterialValidation(ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<MaterialValidation>() ?? NullLogger<MaterialValidation>.Instance;
        this.validator = new(this.LoadValidator);
    }

    /// <summary>
    ///     Checks a source about to be saved against the engine's schema.
    /// </summary>
    /// <param name="source">The source to save.</param>
    /// <returns>
    ///     Why the source cannot be saved, with the code <see cref="MaterialDiagnosticCodes.EditRejected"/>, or
    ///     <see langword="null"/> when it can.
    /// </returns>
    public AssetProblem? ValidateForSave(MaterialSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (!TryWrite(source, out var json, out var problem))
        {
            return problem;
        }

        if (this.validator.Value is not { } schemas)
        {
            return null;
        }

        var engine = schemas.ValidateAgainstEngineSchema(json);
        return engine.IsValid ? null : Rejected("The material does not match the engine schema.", engine.Errors);
    }

    /// <summary>
    ///     Checks an edited source against the engine's schema and the editor's overlay.
    /// </summary>
    /// <param name="source">The edited source.</param>
    /// <returns>Why the edit is rejected, or <see langword="null"/> when it is accepted.</returns>
    public AssetProblem? ValidateEdited(MaterialSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        if (!TryWrite(source, out var json, out var problem))
        {
            return problem;
        }

        if (this.validator.Value is not { } schemas)
        {
            return null;
        }

        var engine = schemas.ValidateAgainstEngineSchema(json);
        var overlay = schemas.ValidateAgainstMergedSchema(json);
        return engine.IsValid && overlay.IsValid
            ? null
            : Rejected("The material does not match the engine schema or the editor's constraints.", engine.Errors.Concat(overlay.Errors));
    }

    // The writer checks the source against the engine schema it embeds, and refuses to write one that does not match.
    private static bool TryWrite(MaterialSource source, [System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out JsonObject? json, [System.Diagnostics.CodeAnalysis.NotNullWhen(false)] out AssetProblem? problem)
    {
        try
        {
            json = MaterialSourceWriter.ToJson(source);
            problem = null;
            return true;
        }
        catch (AssetFormatException exception)
        {
            json = null;
            problem = Rejected(exception.Problem.Message, exception.Problem.Explanation is { } explanation ? [explanation] : []);
            return false;
        }
    }

    private static AssetProblem Rejected(string message, IEnumerable<string> errors)
        => new(DiagnosticSeverity.Error, MaterialDiagnosticCodes.EditRejected, message) { Explanation = string.Join(Environment.NewLine, errors) };

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Schema discovery is optional; the writer still validates every save against the embedded engine schema.")]
    private MaterialSchemaValidator? LoadValidator()
    {
        try
        {
            var loaded = MaterialSchemaValidator.LoadFromAssemblyOutput();
            this.LogSchemaLoaded();
            return loaded;
        }
        catch (Exception exception)
        {
            this.LogSchemaUnavailable(exception);
            return null;
        }
    }
}
