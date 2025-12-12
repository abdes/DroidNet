// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Tree.Model;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Controls.Demo.Tree.Services;

/// <summary>
/// Provides domain model operations for tree item manipulation in the demo app.
/// </summary>
/// <param name="loggerFactory">
///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
internal sealed partial class DomainModelService(ILoggerFactory? loggerFactory = null) : IDomainModelService
{
    private readonly ILogger<DomainModelService> logger = loggerFactory?.CreateLogger<DomainModelService>() ?? NullLogger<DomainModelService>.Instance;

    /// <inheritdoc/>
    public bool TryInsert(ITreeItem item, ITreeItem parent, int index, out string? errorMessage)
    {
        errorMessage = null;
        switch (item)
        {
            case SceneAdapter sceneAdapter when parent is ProjectAdapter projectAdapter:
                {
                    var scenes = projectAdapter.AttachedObject.Scenes;
                    var idx = Math.Clamp(index, 0, scenes.Count);
                    scenes.Insert(idx, sceneAdapter.AttachedObject);
                    return true;
                }

            case EntityAdapter entityAdapter:
                switch (parent)
                {
                    case SceneAdapter parentScene:
                        {
                            var entities = parentScene.AttachedObject.Entities;
                            var idx = Math.Clamp(index, 0, entities.Count);
                            entities.Insert(idx, entityAdapter.AttachedObject);
                            return true;
                        }

                    case EntityAdapter parentEntity:
                        {
                            var entities = parentEntity.AttachedObject.Entities;
                            var idx = Math.Clamp(index, 0, entities.Count);
                            entities.Insert(idx, entityAdapter.AttachedObject);
                            return true;
                        }

                    default:
                        this.LogUnsupportedEntityInsert(parent);
                        errorMessage = "Internal error: Unsupported parent type for entity during add.";
                        return false;
                }

            default:
                // No model update required for other types
                return true;
        }
    }

    /// <inheritdoc/>
    public bool TryRemove(ITreeItem item, ITreeItem parent, out string? errorMessage)
    {
        errorMessage = null;
        switch (item)
        {
            case SceneAdapter sceneAdapter when parent is ProjectAdapter projectAdapter:
                {
                    var project = projectAdapter.AttachedObject;
                    _ = project.Scenes.Remove(sceneAdapter.AttachedObject);
                    return true;
                }

            case EntityAdapter entityAdapter:
                {
                    var entity = entityAdapter.AttachedObject;
                    switch (parent)
                    {
                        case SceneAdapter parentScene:
                            _ = parentScene.AttachedObject.Entities.Remove(entity);
                            return true;

                        case EntityAdapter parentEntity:
                            _ = parentEntity.AttachedObject.Entities.Remove(entity);
                            return true;

                        default:
                            this.LogUnsupportedEntityRemove(parent);
                            errorMessage = "Internal error: Unsupported parent type for entity during remove.";
                            return false;
                    }
                }

            default:
                // nothing to remove
                return true;
        }
    }

    /// <inheritdoc/>
    public bool TryUpdateMoved(TreeItemsMovedEventArgs args, out string? errorMessage)
    {
        errorMessage = null;

        // Update underlying model first: remove all moved items from their previous parents.
        foreach (var move in args.Moves)
        {
            if (!this.TryRemove(move.Item, move.PreviousParent, out var removeErr))
            {
                errorMessage = removeErr ?? "Internal error: Unsupported parent type encountered while removing moved item(s).";

                // Continue with attempts on other items but return false.
            }
        }

        // Then insert into new parents, in increasing index order per parent.
        foreach (var group in args.Moves.GroupBy(m => m.NewParent))
        {
            foreach (var move in group.OrderBy(m => m.NewIndex))
            {
                if (!this.TryInsert(move.Item, move.NewParent, move.NewIndex, out var insertErr))
                {
                    errorMessage = insertErr ?? "Internal error: Unsupported parent type encountered while inserting moved item(s).";
                }
            }
        }

        return string.IsNullOrEmpty(errorMessage);
    }

    /// <inheritdoc/>
    public bool TryRename(ITreeItem item, string newName, out string? errorMessage)
    {
        errorMessage = null;
        switch (item)
        {
            case SceneAdapter sceneAdapter:
                sceneAdapter.AttachedObject.Name = newName;
                return true;

            case EntityAdapter entityAdapter:
                entityAdapter.AttachedObject.Name = newName;
                return true;

            default:
                return true; // renaming adapters without underlying domain object is a no-op for model
        }
    }
}
