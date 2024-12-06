// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Hosting.WinUI;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Storage;

namespace Oxygen.Editor.WorldEditor.ContentBrowser;

/// <summary>
/// Provides services for indexing game assets in the project.
/// </summary>
/// <param name="projectManager">The project manager service.</param>
/// <param name="hostingContext">The hosting context for the application.</param>
public sealed class AssetsIndexingService(IProjectManagerService projectManager, HostingContext hostingContext) : IDisposable
{
    private readonly Subject<GameAsset> assetSubject = new();
    private IDisposable? subscription;

    /// <summary>
    /// Gets the collection of indexed game assets.
    /// </summary>
    public ObservableCollection<GameAsset> Assets { get; } = [];

    /// <summary>
    /// Starts the asynchronous indexing of game assets.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task IndexAssetsAsync()
    {
        this.subscription = this.assetSubject
            .Buffer(TimeSpan.FromSeconds(1), 5)
            .Where(batch => batch.Count > 0)
            .ObserveOn(hostingContext.DispatcherScheduler)
            .Subscribe(
                batch =>
                {
                    foreach (var asset in batch)
                    {
                        this.Assets.Add(asset);
                    }
                },
                HandleError);

        _ = Task.Run(this.BackgroundIndexerAsync);

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.subscription?.Dispose();
        this.assetSubject.Dispose();
    }

    private static void HandleError(Exception ex) =>

        // Handle the error (e.g., log it, show a message to the user, etc.)
        Debug.WriteLine($"Error occurred: {ex.Message}");

    private async Task BackgroundIndexerAsync()
    {
        Debug.Assert(projectManager.CurrentProject is not null, "current should be initialized");

        try
        {
            var folderQueue = new Queue<IFolder>();
            var storageProvider = projectManager.GetCurrentProjectStorageProvider();
            var rootFolder = await storageProvider.GetFolderFromPathAsync(projectManager.CurrentProject.ProjectInfo.Location!).ConfigureAwait(false);
            folderQueue.Enqueue(rootFolder);

            while (folderQueue.Count > 0)
            {
                var currentFolder = folderQueue.Dequeue();
                await foreach (var document in currentFolder.GetDocumentsAsync().ConfigureAwait(false).ConfigureAwait(false).ConfigureAwait(false))
                {
                    var assetName = document.Name.Contains('.', StringComparison.Ordinal)
                        ? document.Name[..document.Name.LastIndexOf('.')]
                        : document.Name;
                    if (string.IsNullOrEmpty(assetName))
                    {
                        continue;
                    }

                    var asset = new GameAsset(assetName, document.Location)
                    {
                        AssetType = GameAsset.GetAssetType(document.Name),
                    };
                    this.assetSubject.OnNext(asset);
                }

                await foreach (var subFolder in currentFolder.GetFoldersAsync().ConfigureAwait(false).ConfigureAwait(false).ConfigureAwait(false))
                {
                    folderQueue.Enqueue(subFolder);
                }
            }

            this.assetSubject.OnCompleted();
        }
#pragma warning disable CA1031 // exceptions forwarded to subscribers via OnError
        catch (Exception ex)
        {
            this.assetSubject.OnError(ex);
        }
#pragma warning restore CA1031
    }
}
