// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

[TestClass]
[TestCategory("Scene Inspector")]
internal sealed class SceneInspectorModuleTests
{
    [TestMethod]
    public void SceneInspectorModule_ContributesThePropertiesPanelAndNoCommand()
    {
        var module = new SceneInspectorModule();

        var panel = module.Panels.Should().ContainSingle().Subject;
        _ = panel.Id.Should().Be(SceneInspectorModule.PanelId);
        _ = panel.ViewModelType.Should().Be<SceneInspectorViewModel>();
        _ = module.Commands.Should().BeEmpty();
    }
}
