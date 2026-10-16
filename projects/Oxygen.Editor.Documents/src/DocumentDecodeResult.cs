// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>The content a kind decoded from a file, or why the file is not a document of that kind.</summary>
public sealed class DocumentDecodeResult
{
    private DocumentDecodeResult(IDocumentContent? content, string? problem)
    {
        this.Content = content;
        this.Problem = problem;
    }

    /// <summary>Gets the decoded content, or <see langword="null"/> when the file did not decode.</summary>
    public IDocumentContent? Content { get; }

    /// <summary>Gets why the file did not decode, as people read it, or <see langword="null"/> when it did.</summary>
    public string? Problem { get; }

    /// <summary>Creates the result of a file that decoded.</summary>
    /// <param name="content">The decoded content.</param>
    /// <returns>The result.</returns>
    public static DocumentDecodeResult Decoded(IDocumentContent content)
    {
        ArgumentNullException.ThrowIfNull(content);
        return new(content, problem: null);
    }

    /// <summary>Creates the result of a file that did not decode.</summary>
    /// <param name="problem">Why the file is not a document of the kind.</param>
    /// <returns>The result.</returns>
    public static DocumentDecodeResult Failed(string problem)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(problem);
        return new(content: null, problem);
    }
}
