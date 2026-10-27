// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.ModelEditor.Importing;
using Oxygen.Editor.Models;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
///     The model family's UI in a project session's scope: the Model Editor of model documents, the Import model
///     dialog, which the Content Explorer opens through <see cref="IModelImportPrompt"/>, and the model facts that
///     Properties shows through <see cref="IModelFacts"/>.
/// </summary>
public sealed class ModelEditorSessionModule : IScopeModule<ISessionLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IModelImportPrompt, ModelImportPrompt>(Reuse.Singleton);
        scope.Register<IModelFacts, ModelFactsReader>(Reuse.Singleton);
        scope.Register<IDocumentEditorProvider, ModelEditorProvider>(Reuse.Singleton);
        scope.Register<ModelEditorServices>(Reuse.Singleton);
    }
}
