// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.MaterialEditor;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises the material picker with real document history and Windows pointer input.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Material color release creates one history entry, while interrupted capture restores the source.</summary>
    /// <param name="drag">Whether to move while the button is held.</param>
    /// <param name="cancel">Whether to interrupt capture before releasing the button.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public Task MaterialSpectrumPointerCompletionPreservesHistory(bool drag, bool cancel) => EnqueueAsync(async () =>
    {
        var directory = Directory.CreateTempSubdirectory("OxygenMaterialPicker-");
        try
        {
            var uri = new Uri("asset:///Content/Materials/Picker.omat.json");
            var resolver = new Mock<IMaterialSourcePathResolver>();
            _ = resolver.Setup(value => value.Resolve(uri)).Returns(new MaterialSourceLocation(uri, directory.FullName, "Content", Path.Combine(directory.FullName, "Picker.omat.json"), "Picker.omat.json"));
            var service = new MaterialDocumentService(
                Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(),
                resolver.Object,
                Mock.Of<IMaterialCookService>(),
                new Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry(),
                new NativeAtomicFileStore(new RealFileSystem()));
            var document = await service.CreateAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(true);
            await service.CloseAsync(document.DocumentId, discard: false, this.TestContext.CancellationToken).ConfigureAwait(true);
            using var model = new MaterialEditorViewModel(new MaterialDocumentMetadata(uri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
            await model.PrepareForCloseAsync().ConfigureAwait(true);
            model.ResumeEditing();
            var flyout = await OpenColorFlyoutAsync(new MaterialEditorView { ViewModel = model }).ConfigureAwait(true);
            try
            {
                await CheckMaterialColorGestureAsync(model, (ColorPicker)flyout.Content, drag, cancel).ConfigureAwait(true);
            }
            finally
            {
                await CloseColorFlyoutAsync(flyout).ConfigureAwait(true);
                await model.CloseAsync(discard: true).ConfigureAwait(true);
            }
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    });

    private static async Task CheckMaterialColorGestureAsync(MaterialEditorViewModel model, ColorPicker picker, bool drag, bool cancel)
    {
        var before = model.BaseColorColor;
        var preview = await DriveSpectrumAsync(picker, drag, cancel).ConfigureAwait(true);
        _ = model.BaseColorColor.Should().Be(cancel ? before : preview);
        _ = model.UndoCommand.CanExecute(parameter: null).Should().Be(!cancel);
        _ = model.IsDirty.Should().Be(!cancel);
        if (!cancel)
        {
            await model.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            _ = model.BaseColorColor.Should().Be(before);
            _ = model.IsDirty.Should().BeFalse();
            _ = model.UndoCommand.CanExecute(parameter: null).Should().BeFalse("one gesture must create exactly one history entry");
            await model.RedoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            _ = model.BaseColorColor.Should().Be(preview);
            _ = model.IsDirty.Should().BeTrue();
        }
    }
}
