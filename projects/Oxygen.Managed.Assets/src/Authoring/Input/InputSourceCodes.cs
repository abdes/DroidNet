// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Input;

/// <summary>
/// The codes of the problems a <c>*.input.json</c> file can have, reported by <see cref="InputSourceReader"/> and
/// <see cref="InputSourceWriter"/>.
/// </summary>
public static class InputSourceCodes
{
    /// <summary>The file is not JSON. The problem has the line and column of the error.</summary>
    public const string NotJson = DiagnosticCodes.InputPrefix + "Source.NotJson";

    /// <summary>The JSON does not match the engine's input schema. The problem is at the value the schema refuses.</summary>
    public const string SchemaMismatch = DiagnosticCodes.InputPrefix + "Source.SchemaMismatch";

    /// <summary>The source declares two actions with one name.</summary>
    public const string ActionDuplicate = DiagnosticCodes.InputPrefix + "Source.ActionDuplicate";

    /// <summary>The source declares two mapping contexts with one name.</summary>
    public const string ContextDuplicate = DiagnosticCodes.InputPrefix + "Source.ContextDuplicate";

    /// <summary>A mapping names, by its name, an action the source does not declare.</summary>
    public const string ActionUndeclared = DiagnosticCodes.InputPrefix + "Source.ActionUndeclared";

    /// <summary>A mapping names, by its virtual path, something that is not an action of an input source.</summary>
    public const string ActionMissing = DiagnosticCodes.InputPrefix + "Source.ActionMissing";

    /// <summary>A rename names an action or context the source does not declare.</summary>
    public const string ProductMissing = DiagnosticCodes.InputPrefix + "Source.ProductMissing";

    /// <summary>A rename gives an action or context a name the source already declares for another one.</summary>
    public const string ProductNameTaken = DiagnosticCodes.InputPrefix + "Source.ProductNameTaken";
}
