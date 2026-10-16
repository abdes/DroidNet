// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>The document a request to open a file got, or why the file did not open.</summary>
public sealed class DocumentOpenResult
{
    private DocumentOpenResult(Document? document, bool wasOpen, string? problem)
    {
        this.Document = document;
        this.WasOpen = wasOpen;
        this.Problem = problem;
    }

    /// <summary>Gets the document that edits the file, or <see langword="null"/> when it did not open.</summary>
    public Document? Document { get; }

    /// <summary>Gets a value indicating whether the file was already open, so the request found its document.</summary>
    public bool WasOpen { get; }

    /// <summary>Gets why the file did not open, as people read it, or <see langword="null"/> when it did.</summary>
    public string? Problem { get; }

    /// <summary>Creates the result of a file that opened.</summary>
    /// <param name="document">The new document.</param>
    /// <returns>The result.</returns>
    internal static DocumentOpenResult Opened(Document document) => new(document, wasOpen: false, problem: null);

    /// <summary>Creates the result of a request that found the document already editing the file.</summary>
    /// <param name="document">The open document.</param>
    /// <returns>The result.</returns>
    internal static DocumentOpenResult AlreadyOpen(Document document) => new(document, wasOpen: true, problem: null);

    /// <summary>Creates the result of a file that did not open.</summary>
    /// <param name="problem">Why it did not open.</param>
    /// <returns>The result.</returns>
    internal static DocumentOpenResult Failed(string problem) => new(document: null, wasOpen: false, problem);
}
