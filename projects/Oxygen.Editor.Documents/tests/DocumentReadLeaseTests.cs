// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>Tests that a read lease keeps a document's content in place while a reader captures its state.</summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public sealed class DocumentReadLeaseTests : DocumentTestBase
{
    [TestMethod]
    public async Task ReloadAsync_WhileALeaseIsHeld_ReplacesTheContentOnlyAfterItIsReleased()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.WriteNote("Wood", "second");
        var lease = document.AcquireReadLease()!;

        var reload = document.ReloadAsync(this.Token);
        await Task.Delay(50, this.Token).ConfigureAwait(false);

        _ = reload.IsCompleted.Should().BeFalse();
        _ = document.Content.Should().BeSameAs(note);
        lease.Dispose();
        _ = (await reload.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        _ = ((NoteContent)document.Content).Text.Should().Be("second");
        _ = note.IsDisposed.Should().BeTrue();
    }

    [TestMethod]
    public async Task ReloadAsync_WithTwoLeases_WaitsForBothEvenIfOneIsReleasedTwice()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.WriteNote("Wood", "second");
        var first = document.AcquireReadLease()!;
        var second = document.AcquireReadLease()!;

        var reload = document.ReloadAsync(this.Token);
        first.Dispose();
        first.Dispose();
        await Task.Delay(50, this.Token).ConfigureAwait(false);

        _ = reload.IsCompleted.Should().BeFalse();
        _ = document.Content.Should().BeSameAs(note);
        second.Dispose();
        _ = (await reload.ConfigureAwait(false)).Succeeded.Should().BeTrue();
    }

    [TestMethod]
    public async Task ReloadAsync_CancelledWhileWaitingForALease_KeepsTheContent()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.WriteNote("Wood", "second");
        using var lease = document.AcquireReadLease();
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(this.Token);

        var reload = document.ReloadAsync(cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);
        var wait = async () => await reload.ConfigureAwait(false);

        _ = await wait.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = document.Content.Should().BeSameAs(note);
        _ = note.IsDisposed.Should().BeFalse();
    }

    [TestMethod]
    public async Task Close_WhileALeaseIsHeld_DisposesTheContentWhenTheLeaseIsReleased()
    {
        var (document, note) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        var lease = document.AcquireReadLease()!;

        this.Documents.Close(document);

        _ = note.IsDisposed.Should().BeFalse();
        lease.Dispose();
        _ = note.IsDisposed.Should().BeTrue();
    }

    [TestMethod]
    public async Task AcquireReadLease_AfterClose_ReturnsNull()
    {
        var (document, _) = await this.OpenNoteAsync("Wood").ConfigureAwait(false);
        this.Documents.Close(document);

        var lease = document.AcquireReadLease();

        _ = lease.Should().BeNull();
    }
}
