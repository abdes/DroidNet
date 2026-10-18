// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Diagnostics;

/// <summary>
/// A problem of an authored asset file: what is wrong, the contract code that names it, and where it is.
/// </summary>
/// <remarks>
/// Each asset family specializes it with what only that family knows. Tests and tools compare <see cref="Code"/>,
/// never <see cref="Message"/>.
/// </remarks>
public record AssetProblem
{
    /// <summary>Initializes a new instance of the <see cref="AssetProblem"/> class.</summary>
    /// <param name="severity">How serious the problem is.</param>
    /// <param name="code">The contract code that names the problem.</param>
    /// <param name="message">What is wrong, for the user.</param>
    /// <exception cref="ArgumentException"><paramref name="code"/> or <paramref name="message"/> is empty.</exception>
    public AssetProblem(DiagnosticSeverity severity, string code, string message)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(code);
        ArgumentException.ThrowIfNullOrWhiteSpace(message);
        this.Severity = severity;
        this.Code = code;
        this.Message = message;
    }

    /// <summary>Gets how serious the problem is.</summary>
    public DiagnosticSeverity Severity { get; }

    /// <summary>Gets the contract code that names the problem.</summary>
    public string Code { get; }

    /// <summary>Gets what is wrong, for the user.</summary>
    public string Message { get; }

    /// <summary>Gets the path of the file that has the problem, when the producer knows it.</summary>
    public string? File { get; init; }

    /// <summary>Gets the one-based line of the problem in <see cref="File"/>, when known.</summary>
    public int? Line { get; init; }

    /// <summary>Gets the one-based column of the problem on <see cref="Line"/>, when known.</summary>
    public int? Column { get; init; }

    /// <summary>Gets the details behind <see cref="Message"/>, such as the rules a value broke.</summary>
    public string? Explanation { get; init; }
}
