// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Managed.Assets.Authoring;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

[TestClass]
public sealed class InputSourceTests
{
    private const string Player = """
        {
          // Comments and trailing commas are accepted, as the cooker accepts them.
          "actions": [
            { "name": "Jump", "type": "bool", "consumes_input": true },
            { "name": "Move", "type": "axis2d", "accumulation": "cumulative" },
          ],
          "contexts": [
            {
              "name": "OnFoot",
              "priority": 3,
              "mappings": [
                { "action": "Jump", "slot": "Space", "trigger": "pressed" },
                { "action": "/Content/Input/Common.input/Fire.oiact", "slot": "F" },
                {
                  "action": "Move",
                  "slot": "W",
                  "triggers": [
                    { "type": "chord", "chord_action": "Jump" },
                    { "type": "combo", "combo_actions": [ { "action": "Jump" } ] }
                  ]
                }
              ]
            }
          ]
        }
        """;

    private static readonly AssetPath Source = AssetPath.Parse("/Content/Input/Player.input");

    [TestMethod]
    public void ReadDeclaresActionsAndContextsWithTheirPositions()
    {
        var source = InputSourceReader.Read(Encoding.UTF8.GetBytes(Player));

        _ = source.Actions.Should().Equal(
            new InputActionDeclaration("Jump", "bool") { ConsumesInput = true, Position = new SourcePosition(4, 15) },
            new InputActionDeclaration("Move", "axis2d") { Accumulation = "cumulative", Position = new SourcePosition(5, 15) });
        var context = source.Contexts.Should().ContainSingle().Subject;
        _ = context.Name.Should().Be("OnFoot");
        _ = context.Priority.Should().Be(3);
        _ = context.Position.Should().Be(new SourcePosition(9, 15));
        _ = source.FindDeclaration("OnFoot", AssetFileTypes.InputMappingContext).Should().Be(new SourcePosition(9, 15));
        _ = source.FindDeclaration("OnFoot", AssetFileTypes.InputAction).Should().BeNull();
    }

    [TestMethod]
    public void ReferencesNameEveryActionAMappingUses()
    {
        var source = InputSourceReader.Read(Encoding.UTF8.GetBytes(Player));

        _ = source.ActionReferences.Select(static reference => reference.Text).Should().Equal(
            "Jump", "/Content/Input/Common.input/Fire.oiact", "Move", "Jump", "Jump");
        _ = source.ActionReferences.First().Position.Should().Be(new SourcePosition(12, 21));
        var actions = source.ActionReferences.Select(reference => reference.TryGetAction(Source, out var action) ? action.VirtualPath : null);
        _ = actions.Should().StartWith(["/Content/Input/Player.input/Jump.oiact", "/Content/Input/Common.input/Fire.oiact"]);
    }

    [TestMethod]
    public void ProductsAreNamedUnderTheirSource()
        => _ = InputSource.ProductIdentity(Source, "Jump", AssetFileTypes.InputAction).Should().Be(AssetPath.Parse("/Content/Input/Player.input/Jump.oiact"));

    [TestMethod]
    [DataRow("""{ "actions": [ { "name": "Jump", "type": "bool" } ] }""")]
    [DataRow("{}")]
    public void ASourceMayDeclareOnlyActionsOrNothing(string json)
        => _ = InputSourceReader.Check(Encoding.UTF8.GetBytes(json)).Problems.Should().BeEmpty();

    [TestMethod]
    public void NotJsonIsReportedAtItsPosition()
    {
        var problem = InputSourceReader.Check("{\n  \"actions\": [ }"u8).Problems.Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(InputSourceCodes.NotJson);
        _ = problem.Line.Should().Be(2);
    }

    [TestMethod]
    public void ASchemaProblemIsReportedAtTheValueItRefuses()
    {
        var report = InputSourceReader.Check("""
            {
              "contexts": [
                { "name": "OnFoot", "mappings": [ { "action": "Jump", "slot": "NotAKey" } ] }
              ]
            }
            """u8);

        _ = report.Source.Should().BeNull();
        _ = report.Problems.Should().NotBeEmpty().And.OnlyContain(static problem => problem.Code == InputSourceCodes.SchemaMismatch);
        _ = report.Problems.Should().Contain(static problem => problem.Line == 3);
    }

    [TestMethod]
    public void AnActionNamedByAPathOfAnotherTypeIsRefusedByTheSchema()
    {
        var report = InputSourceReader.Check("""
            { "contexts": [ { "name": "OnFoot", "mappings": [ { "action": "/Content/Input/Common.input/Menu.oimap", "slot": "F" } ] } ] }
            """u8);

        _ = report.Problems.Should().NotBeEmpty().And.OnlyContain(static problem => problem.Code == InputSourceCodes.SchemaMismatch);
    }

    [TestMethod]
    public void DuplicatesAndUndeclaredNamesAreReportedWhereTheyAre()
    {
        var report = InputSourceReader.Check("""
            {
              "actions": [ { "name": "Jump", "type": "bool" }, { "name": "Jump", "type": "bool" } ],
              "contexts": [
                { "name": "OnFoot", "mappings": [ { "action": "Dash", "slot": "LeftShift" } ] },
                { "name": "OnFoot", "mappings": [] }
              ]
            }
            """u8);

        _ = report.Source.Should().NotBeNull();
        _ = report.Problems.Select(static problem => (problem.Code, problem.Line)).Should().Equal(
            (InputSourceCodes.ActionDuplicate, 2),
            (InputSourceCodes.ContextDuplicate, 5),
            (InputSourceCodes.ActionUndeclared, 4));
    }

    [TestMethod]
    public void RenamingAnActionRenamesItsLocalReferences()
    {
        var renamed = InputSourceWriter.RenameProduct(Encoding.UTF8.GetBytes(Player), AssetFileTypes.InputAction, "Jump", "Leap");

        var source = InputSourceReader.Read(JsonSerializer.SerializeToUtf8Bytes(renamed));
        _ = source.Actions.Select(static action => action.Name).Should().Equal("Leap", "Move");
        _ = source.ActionReferences.Select(static reference => reference.Text).Should().Equal(
            "Leap", "/Content/Input/Common.input/Fire.oiact", "Move", "Leap", "Leap");
        _ = renamed["contexts"]![0]!["priority"]!.GetValue<int>().Should().Be(3);
    }

    [TestMethod]
    public void RenamingAContextKeepsTheActions()
    {
        var renamed = InputSourceWriter.RenameProduct(Encoding.UTF8.GetBytes(Player), AssetFileTypes.InputMappingContext, "OnFoot", "Driving");

        var source = InputSourceReader.Read(JsonSerializer.SerializeToUtf8Bytes(renamed));
        _ = source.Contexts.Select(static context => context.Name).Should().Equal("Driving");
        _ = source.ActionReferences.Select(static reference => reference.Text).Should().StartWith(["Jump"]);
    }

    [TestMethod]
    [DataRow("Dash", "Leap", InputSourceCodes.ProductMissing)]
    [DataRow("Jump", "move", InputSourceCodes.ProductNameTaken)]
    [DataRow("Jump", "Le:ap", "vpath.segment.reserved_character")]
    public void ARenameTheSourceCannotTakeIsRefused(string name, string newName, string code)
    {
        var refuse = () => InputSourceWriter.RenameProduct(Encoding.UTF8.GetBytes(Player), AssetFileTypes.InputAction, name, newName);

        _ = refuse.Should().Throw<AssetFormatException>().Which.Problem.Code.Should().Be(code);
    }
}
