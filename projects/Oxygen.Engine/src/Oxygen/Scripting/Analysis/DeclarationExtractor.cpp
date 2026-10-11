//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <Luau/Ast.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <fmt/format.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scripting/Analysis/DeclarationExtractor.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>

namespace oxygen::scripting::detail {

namespace {

  using data::pak::scripting::ScriptParamType;

  constexpr std::string_view kOxygenGlobal = "oxygen";
  constexpr std::string_view kScriptMember = "script";
  constexpr std::string_view kParamMember = "param";
  constexpr std::string_view kParamsField = "params";

  struct ParamTypeInfo {
    std::string_view name;
    ScriptParamType type = ScriptParamType::kNone;
    std::size_t vector_size = 0;
  };

  constexpr auto kParamTypes = std::to_array<ParamTypeInfo>({
    { .name = "bool", .type = ScriptParamType::kBool, .vector_size = 0 },
    { .name = "int", .type = ScriptParamType::kInt32, .vector_size = 0 },
    { .name = "float", .type = ScriptParamType::kFloat, .vector_size = 0 },
    { .name = "string", .type = ScriptParamType::kString, .vector_size = 0 },
    { .name = "vec2", .type = ScriptParamType::kVec2, .vector_size = 2 },
    { .name = "vec3", .type = ScriptParamType::kVec3, .vector_size = 3 },
    { .name = "vec4", .type = ScriptParamType::kVec4, .vector_size = 4 },
    { .name = "asset", .type = ScriptParamType::kAsset, .vector_size = 0 },
    { .name = "node", .type = ScriptParamType::kNode, .vector_size = 0 },
  });

  struct AssetKindInfo {
    std::string_view name;
    data::AssetType type = data::AssetType::kUnknown;
  };

  constexpr auto kAssetKinds = std::to_array<AssetKindInfo>({
    { .name = "material", .type = data::AssetType::kMaterial },
    { .name = "geometry", .type = data::AssetType::kGeometry },
    { .name = "scene", .type = data::AssetType::kScene },
    { .name = "script", .type = data::AssetType::kScript },
    { .name = "input_action", .type = data::AssetType::kInputAction },
    {
      .name = "input_mapping_context",
      .type = data::AssetType::kInputMappingContext,
    },
    { .name = "physics_material", .type = data::AssetType::kPhysicsMaterial },
    { .name = "collision_shape", .type = data::AssetType::kCollisionShape },
    { .name = "physics_scene", .type = data::AssetType::kPhysicsScene },
  });

  enum class FieldId : uint8_t {
    kDefault,
    kMin,
    kMax,
    kStep,
    kUnit,
    kDescription,
    kOptions,
    kColor,
    kKind,
  };

  struct FieldInfo {
    std::string_view name;
    FieldId id = FieldId::kDefault;
  };

  constexpr auto kFields = std::to_array<FieldInfo>({
    { .name = "default", .id = FieldId::kDefault },
    { .name = "min", .id = FieldId::kMin },
    { .name = "max", .id = FieldId::kMax },
    { .name = "step", .id = FieldId::kStep },
    { .name = "unit", .id = FieldId::kUnit },
    { .name = "description", .id = FieldId::kDescription },
    { .name = "options", .id = FieldId::kOptions },
    { .name = "color", .id = FieldId::kColor },
    { .name = "kind", .id = FieldId::kKind },
  });

  //! Literal value of a declaration field.
  struct Literal {
    enum class Kind : uint8_t { kBool, kNumber, kString, kList };

    Kind kind = Kind::kBool;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Literal> items;
  };

  //! A literal, or the first sub-expression that is not one.
  struct LiteralResult {
    std::optional<Literal> literal;
    const Luau::AstExpr* computed = nullptr;
  };

  auto MakeLiteral(const Literal::Kind kind) -> Literal
  {
    return Literal {
      .kind = kind,
      .boolean = false,
      .number = 0.0,
      .text = {},
      .items = {},
    };
  }

  auto Found(Literal literal) -> LiteralResult
  {
    return LiteralResult { .literal = std::move(literal), .computed = nullptr };
  }

  auto Computed(const Luau::AstExpr& expr) -> LiteralResult
  {
    return LiteralResult { .literal = std::nullopt, .computed = &expr };
  }

  auto AsStringView(const Luau::AstArray<char>& value) -> std::string_view
  {
    return { value.data, value.size };
  }

  template <typename T>
  auto AsSpan(const Luau::AstArray<T>& array) -> std::span<T>
  {
    return { array.data, array.size };
  }

  auto IsGlobal(const Luau::AstExpr& expr, const std::string_view name) -> bool
  {
    const auto* const global = expr.as<Luau::AstExprGlobal>();
    return global != nullptr && global->name.value != nullptr
      && name == global->name.value;
  }

  //! Returns `member` when `expr` is `<object>.<member>` and `object` matches.
  auto IndexedMember(const Luau::AstExpr& expr) -> const Luau::AstExprIndexName*
  {
    const auto* const index = expr.as<Luau::AstExprIndexName>();
    return index != nullptr && index->index.value != nullptr ? index : nullptr;
  }

  auto IsOxygenScriptCall(const Luau::AstExprCall& call) -> bool
  {
    const auto* const member = IndexedMember(*call.func);
    return member != nullptr && kScriptMember == member->index.value
      && IsGlobal(*member->expr, kOxygenGlobal);
  }

  //! Returns the type name of an `oxygen.param.<type>` callee.
  auto ParamTypeName(const Luau::AstExprCall& call)
    -> std::optional<std::string_view>
  {
    const auto* const member = IndexedMember(*call.func);
    if (member == nullptr) {
      return std::nullopt;
    }
    const auto* const param = IndexedMember(*member->expr);
    if (param == nullptr || kParamMember != param->index.value
      || !IsGlobal(*param->expr, kOxygenGlobal)) {
      return std::nullopt;
    }
    return std::string_view(member->index.value);
  }

  auto EvaluateLiteral(const Luau::AstExpr& expr) -> LiteralResult
  {
    if (const auto* const value = expr.as<Luau::AstExprConstantBool>()) {
      auto literal = MakeLiteral(Literal::Kind::kBool);
      literal.boolean = value->value;
      return Found(std::move(literal));
    }
    if (const auto* const value = expr.as<Luau::AstExprConstantNumber>()) {
      auto literal = MakeLiteral(Literal::Kind::kNumber);
      literal.number = value->value;
      return Found(std::move(literal));
    }
    if (const auto* const value = expr.as<Luau::AstExprConstantInteger>()) {
      auto literal = MakeLiteral(Literal::Kind::kNumber);
      literal.number = static_cast<double>(value->value);
      return Found(std::move(literal));
    }
    if (const auto* const value = expr.as<Luau::AstExprConstantString>()) {
      auto literal = MakeLiteral(Literal::Kind::kString);
      literal.text = std::string(AsStringView(value->value));
      return Found(std::move(literal));
    }
    if (const auto* const unary = expr.as<Luau::AstExprUnary>()) {
      if (unary->op == Luau::AstExprUnary::Op::Minus) {
        auto operand = EvaluateLiteral(*unary->expr);
        if (operand.literal
          && operand.literal->kind == Literal::Kind::kNumber) {
          operand.literal->number = -operand.literal->number;
          return operand;
        }
      }
      return Computed(expr);
    }
    if (const auto* const table = expr.as<Luau::AstExprTable>()) {
      auto list = MakeLiteral(Literal::Kind::kList);
      for (const auto& item : table->items) {
        if (item.kind != Luau::AstExprTable::Item::Kind::List) {
          return Computed(expr);
        }
        auto element = EvaluateLiteral(*item.value);
        if (!element.literal) {
          return element;
        }
        list.items.push_back(std::move(*element.literal));
      }
      return Found(std::move(list));
    }
    return Computed(expr);
  }

  auto IsNumberList(const Literal& literal, const std::size_t size) -> bool
  {
    return literal.kind == Literal::Kind::kList && literal.items.size() == size
      && std::ranges::all_of(literal.items, [](const Literal& item) -> bool {
           return item.kind == Literal::Kind::kNumber;
         });
  }

  auto IsWholeInt32(const double value) -> bool
  {
    return std::trunc(value) == value
      && value >= static_cast<double>(std::numeric_limits<int32_t>::min())
      && value <= static_cast<double>(std::numeric_limits<int32_t>::max());
  }

  auto ZeroValue(const ParamTypeInfo& type) -> data::ScriptParam
  {
    switch (type.type) {
    case ScriptParamType::kBool:
      return false;
    case ScriptParamType::kInt32:
      return int32_t { 0 };
    case ScriptParamType::kFloat:
      return 0.0F;
    case ScriptParamType::kString:
      return std::string {};
    case ScriptParamType::kVec2:
      return Vec2 { 0.0F };
    case ScriptParamType::kVec3:
      return Vec3 { 0.0F };
    case ScriptParamType::kVec4:
      return Vec4 { 0.0F };
    case ScriptParamType::kAsset:
      return data::ScriptAssetParam {};
    case ScriptParamType::kNode:
      return data::ScriptNodeParam {};
    case ScriptParamType::kNone:
      break;
    }
    return false;
  }

  auto IsNumeric(const ParamTypeInfo& type) -> bool
  {
    return type.type == ScriptParamType::kInt32
      || type.type == ScriptParamType::kFloat;
  }

  auto FieldApplies(const FieldId field, const ParamTypeInfo& type) -> bool
  {
    switch (field) {
    case FieldId::kDefault:
      return type.type != ScriptParamType::kAsset
        && type.type != ScriptParamType::kNode;
    case FieldId::kMin:
    case FieldId::kMax:
    case FieldId::kStep:
      return IsNumeric(type);
    case FieldId::kUnit:
      return IsNumeric(type) || type.vector_size != 0;
    case FieldId::kDescription:
      return true;
    case FieldId::kOptions:
      return type.type == ScriptParamType::kString;
    case FieldId::kColor:
      return type.type == ScriptParamType::kVec3
        || type.type == ScriptParamType::kVec4;
    case FieldId::kKind:
      return type.type == ScriptParamType::kAsset;
    }
    return false;
  }

  auto ToVector(const Literal& literal) -> data::ScriptParam
  {
    const auto at = [&literal](const std::size_t i) -> float {
      return static_cast<float>(literal.items.at(i).number);
    };
    switch (literal.items.size()) {
    case 2:
      return Vec2 { at(0), at(1) };
    case 3:
      return Vec3 { at(0), at(1), at(2) };
    default:
      return Vec4 { at(0), at(1), at(2), at(3) };
    }
  }

  //! Validates one `oxygen.script` declaration and collects its parameters.
  class Extractor final {
  public:
    Extractor(
      const std::string_view source_name, std::vector<ScriptDiagnostic>& out)
      : source_name_(source_name)
      , diagnostics_(&out)
      , first_error_(out.size())
    {
    }

    auto Extract(Luau::AstStatBlock& root) -> ExtractedDeclarations;

  private:
    struct Report {
      DiagnosticCode code = DiagnosticCode::kSyntaxError;
      std::string message;
      Luau::Location location;
      std::string label;
      std::string help;
      std::string note;
    };

    [[nodiscard]] auto Span(const Luau::Location& location) const -> SourceSpan;
    auto Add(Report report) -> void;
    auto AddWithSecondary(Report report, const Luau::Location& secondary,
      std::string secondary_label) -> void;
    [[nodiscard]] auto HasErrors() const -> bool;

    auto CheckReturned(const Luau::AstStatBlock& root,
      const Luau::AstStatLocal& declaration) -> void;
    auto ExtractScriptTable(const Luau::AstExprCall& call) -> void;
    auto ExtractParams(const Luau::AstExprTable& params) -> void;
    auto ExtractParam(std::string_view name, const Luau::Location& name_span,
      const Luau::AstExpr& value) -> void;
    auto ApplyField(ScriptParamDeclaration& declaration,
      const ParamTypeInfo& type, const FieldInfo& field,
      const Luau::AstExpr& value) -> void;
    auto ApplyDefault(ScriptParamDeclaration& declaration,
      const ParamTypeInfo& type, const Literal& literal,
      const Luau::Location& location) -> void;
    auto CheckConsistency(const ScriptParamDeclaration& declaration,
      const ParamTypeInfo& type, const Luau::AstExprCall& call,
      const std::optional<Luau::Location>& default_location) -> void;

    std::string_view source_name_;
    observer_ptr<std::vector<ScriptDiagnostic>> diagnostics_;
    std::size_t first_error_ = 0;
    std::vector<ScriptParamDeclaration> declarations_;
    //! Name location of each entry of `declarations_`.
    std::vector<Luau::Location> name_locations_;
  };

  //! Collects every `oxygen.script` call in a chunk, at any depth.
  class ScriptCallCollector final : public Luau::AstVisitor {
  public:
    auto visit(Luau::AstExprCall* node) -> bool override
    {
      if (IsOxygenScriptCall(*node)) {
        calls.push_back(node);
      }
      return true;
    }

    std::vector<const Luau::AstExprCall*> calls;
  };

  auto Extractor::Span(const Luau::Location& location) const -> SourceSpan
  {
    return ToSpan(source_name_, location);
  }

  auto Extractor::Add(Report report) -> void
  {
    auto diagnostic = ScriptDiagnostic {
      .severity = DiagnosticSeverity::kError,
      .code = report.code,
      .message = std::move(report.message),
      .labels = {
        DiagnosticLabel {
          .span = Span(report.location),
          .message = std::move(report.label),
          .primary = true,
        },
      },
      .notes = {},
      .help = std::nullopt,
      .slot = std::nullopt,
    };
    if (!report.note.empty()) {
      diagnostic.notes.push_back(std::move(report.note));
    }
    if (!report.help.empty()) {
      diagnostic.help = Suggestion {
        .message = std::move(report.help),
        .span = std::nullopt,
        .replacement = std::nullopt,
      };
    }
    diagnostics_->push_back(std::move(diagnostic));
  }

  auto Extractor::AddWithSecondary(Report report,
    const Luau::Location& secondary, std::string secondary_label) -> void
  {
    Add(std::move(report));
    diagnostics_->back().labels.push_back(DiagnosticLabel {
      .span = Span(secondary),
      .message = std::move(secondary_label),
      .primary = false,
    });
  }

  auto Extractor::HasErrors() const -> bool
  {
    return std::ranges::any_of(
      diagnostics_->begin() + static_cast<std::ptrdiff_t>(first_error_),
      diagnostics_->end(), [](const ScriptDiagnostic& diagnostic) -> bool {
        return diagnostic.severity == DiagnosticSeverity::kError;
      });
  }

  auto Extractor::Extract(Luau::AstStatBlock& root) -> ExtractedDeclarations
  {
    auto collector = ScriptCallCollector {};
    root.visit(&collector);
    if (collector.calls.empty()) {
      return {};
    }

    const Luau::AstStatLocal* declaration = nullptr;
    const Luau::AstExprCall* declaration_call = nullptr;
    for (const auto* const call : collector.calls) {
      const auto* const it = std::ranges::find_if(
        root.body, [call](const Luau::AstStat* stat) -> bool {
          const auto* const local = stat->as<Luau::AstStatLocal>();
          return local != nullptr && local->vars.size == 1
            && local->values.size == 1 && AsSpan(local->values).front() == call;
        });
      if (it == root.body.end()) {
        Add({
          .code = DiagnosticCode::kDeclarationNotTopLevelLocal,
          .message = "`oxygen.script` must initialize a top-level local",
          .location = call->location,
          .label = "declared here",
          .help = "write `local Script = oxygen.script { ... }` at the top "
                  "level of the script",
          .note = "declarations are read without running the script",
        });
        continue;
      }
      if (declaration != nullptr) {
        AddWithSecondary(
          {
            .code = DiagnosticCode::kDuplicateDeclaration,
            .message = "script declares `oxygen.script` more than once",
            .location = call->location,
            .label = "second declaration",
            .help = "keep one declaration per script",
            .note = {},
          },
          declaration_call->location, "first declaration");
        continue;
      }
      declaration = (*it)->as<Luau::AstStatLocal>();
      declaration_call = call;
    }

    if (declaration != nullptr) {
      CheckReturned(root, *declaration);
      ExtractScriptTable(*declaration_call);
    }

    if (HasErrors()) {
      return { .has_declaration = true, .declarations = {} };
    }
    return {
      .has_declaration = true,
      .declarations = std::move(declarations_),
    };
  }

  auto Extractor::CheckReturned(const Luau::AstStatBlock& root,
    const Luau::AstStatLocal& declaration) -> void
  {
    const auto* const local = AsSpan(declaration.vars).front();
    const auto* const last
      = root.body.size == 0 ? nullptr : AsSpan(root.body).back();
    const auto* const ret
      = last == nullptr ? nullptr : last->as<Luau::AstStatReturn>();
    if (ret != nullptr && ret->list.size == 1) {
      const auto* const returned
        = AsSpan(ret->list).front()->as<Luau::AstExprLocal>();
      if (returned != nullptr && returned->local == local) {
        return;
      }
    }
    const auto name = std::string_view(local->name.value);
    AddWithSecondary(
      {
        .code = DiagnosticCode::kDeclarationNotReturned,
        .message = fmt::format("the script must return `{}`", name),
        .location = ret != nullptr
          ? ret->location
          : Luau::Location(root.location.end, root.location.end),
        .label = ret != nullptr ? "returns something else" : "no return",
        .help = fmt::format("end the script with `return {}`", name),
        .note = {},
      },
      local->location, "script table declared here");
  }

  auto Extractor::ExtractScriptTable(const Luau::AstExprCall& call) -> void
  {
    const auto* const table = call.args.size == 1
      ? AsSpan(call.args).front()->as<Luau::AstExprTable>()
      : nullptr;
    if (table == nullptr) {
      Add({
        .code = DiagnosticCode::kDeclarationNotTable,
        .message = "`oxygen.script` expects a table constructor",
        .location = call.argLocation,
        .label = "expected `{ params = { ... } }`",
        .help = {},
        .note = {},
      });
      return;
    }

    for (const auto& item : table->items) {
      const auto* const key = item.key == nullptr
        ? nullptr
        : item.key->as<Luau::AstExprConstantString>();
      if (item.kind != Luau::AstExprTable::Item::Kind::Record || key == nullptr
        || AsStringView(key->value) != kParamsField) {
        const auto& location
          = item.key != nullptr ? item.key->location : item.value->location;
        Add({
          .code = DiagnosticCode::kUnknownField,
          .message = "unknown field in `oxygen.script` declaration",
          .location = location,
          .label = "not a declaration field",
          .help = "the declaration has one field, `params`",
          .note = {},
        });
        continue;
      }
      const auto* const params = item.value->as<Luau::AstExprTable>();
      if (params == nullptr) {
        Add({
          .code = DiagnosticCode::kDeclarationNotTable,
          .message = "`params` must be a table constructor",
          .location = item.value->location,
          .label = "expected `{ name = oxygen.param.<type> { ... } }`",
          .help = {},
          .note = "declarations are read without running the script",
        });
        continue;
      }
      ExtractParams(*params);
    }
  }

  auto Extractor::ExtractParams(const Luau::AstExprTable& params) -> void
  {
    for (const auto& item : params.items) {
      const auto* const key = item.key == nullptr
        ? nullptr
        : item.key->as<Luau::AstExprConstantString>();
      if (item.kind != Luau::AstExprTable::Item::Kind::Record
        || key == nullptr) {
        Add({
          .code = DiagnosticCode::kParamNotDeclared,
          .message = "parameters must be written `name = oxygen.param.<type> "
                     "{ ... }`",
          .location
          = item.key != nullptr ? item.key->location : item.value->location,
          .label = "expected a named parameter",
          .help = {},
          .note = {},
        });
        continue;
      }
      ExtractParam(AsStringView(key->value), key->location, *item.value);
    }
  }

  auto Extractor::ExtractParam(const std::string_view name,
    const Luau::Location& name_span, const Luau::AstExpr& value) -> void
  {
    if (name.size() > ScriptAnalyzer::kMaxParamNameBytes) {
      Add({
        .code = DiagnosticCode::kInvalidParamName,
        .message = fmt::format("parameter name is longer than {} bytes",
          ScriptAnalyzer::kMaxParamNameBytes),
        .location = name_span,
        .label = fmt::format("{} bytes", name.size()),
        .help = {},
        .note = {},
      });
      return;
    }

    const auto previous
      = std::ranges::find(declarations_, name, &ScriptParamDeclaration::name);
    const auto* const call = value.as<Luau::AstExprCall>();
    const auto type_name
      = call != nullptr ? ParamTypeName(*call) : std::nullopt;
    if (!type_name) {
      Add({
        .code = DiagnosticCode::kParamNotDeclared,
        .message = fmt::format(
          "parameter `{}` must be declared with `oxygen.param.<type>`", name),
        .location = value.location,
        .label = "not a parameter declaration",
        .help = fmt::format(
          "for example `{} = oxygen.param.float {{ default = 1 }}`", name),
        .note = {},
      });
      return;
    }

    const auto type = std::ranges::find_if(
      kParamTypes, [&type_name](const ParamTypeInfo& info) -> bool {
        return info.name == *type_name;
      });
    if (type == kParamTypes.end()) {
      Add({
        .code = DiagnosticCode::kUnknownParamType,
        .message = fmt::format("unknown parameter type `{}`", *type_name),
        .location = call->func->location,
        .label = "unknown type",
        .help = "use bool, int, float, string, vec2, vec3, vec4, asset or node",
        .note = {},
      });
      return;
    }

    const auto* const fields = call->args.size == 1
      ? AsSpan(call->args).front()->as<Luau::AstExprTable>()
      : nullptr;
    if (fields == nullptr) {
      Add({
        .code = DiagnosticCode::kParamNotDeclared,
        .message = fmt::format(
          "`oxygen.param.{}` expects a table constructor", *type_name),
        .location = call->argLocation,
        .label = "expected `{ ... }`",
        .help = {},
        .note = {},
      });
      return;
    }

    auto declaration = ScriptParamDeclaration {
      .name = std::string(name),
      .type = type->type,
      .default_value = ZeroValue(*type),
      .min = std::nullopt,
      .max = std::nullopt,
      .step = std::nullopt,
      .unit = {},
      .description = {},
      .options = {},
      .color = false,
      .span = Span(name_span),
    };
    const auto errors_before = diagnostics_->size();
    auto default_location = std::optional<Luau::Location> {};
    for (const auto& item : fields->items) {
      const auto* const key = item.key == nullptr
        ? nullptr
        : item.key->as<Luau::AstExprConstantString>();
      const auto key_name
        = key != nullptr ? AsStringView(key->value) : std::string_view {};
      const auto field = std::ranges::find(kFields, key_name, &FieldInfo::name);
      if (item.kind != Luau::AstExprTable::Item::Kind::Record
        || field == kFields.end()) {
        Add({
          .code = DiagnosticCode::kUnknownField,
          .message = "unknown parameter field",
          .location
          = item.key != nullptr ? item.key->location : item.value->location,
          .label = "not a parameter field",
          .help = "fields are default, min, max, step, unit, description, "
                  "options, color and kind",
          .note = {},
        });
        continue;
      }
      if (!FieldApplies(field->id, *type)) {
        Add({
          .code = DiagnosticCode::kFieldNotApplicable,
          .message = fmt::format(
            "`{}` does not apply to `{}` parameters", field->name, type->name),
          .location = key->location,
          .label = "not applicable",
          .help = {},
          .note = field->id == FieldId::kDefault
            ? "asset and node parameters have no default; they are set per "
              "slot"
            : std::string {},
        });
        continue;
      }
      if (field->id == FieldId::kDefault) {
        default_location = item.value->location;
      }
      ApplyField(declaration, *type, *field, *item.value);
    }
    // Cross-field checks only make sense once every field was valid.
    if (diagnostics_->size() != errors_before) {
      return;
    }
    CheckConsistency(declaration, *type, *call, default_location);

    if (previous != declarations_.end()) {
      AddWithSecondary(
        {
          .code = DiagnosticCode::kDuplicateParam,
          .message = fmt::format("parameter `{}` is declared twice", name),
          .location = name_span,
          .label = "declared again here",
          .help = {},
          .note = {},
        },
        name_locations_.at(
          static_cast<std::size_t>(previous - declarations_.begin())),
        "first declared here");
      return;
    }
    declarations_.push_back(std::move(declaration));
    name_locations_.push_back(name_span);
  }

  auto Extractor::ApplyField(ScriptParamDeclaration& declaration,
    const ParamTypeInfo& type, const FieldInfo& field,
    const Luau::AstExpr& value) -> void
  {
    const auto evaluated = EvaluateLiteral(value);
    if (!evaluated.literal) {
      Add({
        .code = DiagnosticCode::kValueNotLiteral,
        .message
        = fmt::format("parameter {} must be a literal value", field.name),
        .location = evaluated.computed->location,
        .label = "computed here",
        .help
        = fmt::format("write the value, for example `{} = {}`", field.name,
          field.id == FieldId::kUnit || field.id == FieldId::kDescription
              || field.id == FieldId::kKind
            ? "\"text\""
            : "3.2"),
        .note = "declarations are read without running the script",
      });
      return;
    }
    const auto& literal = *evaluated.literal;
    const auto mismatch
      = [this, &field, &value](std::string_view expected) -> void {
      Add({
        .code = DiagnosticCode::kFieldTypeMismatch,
        .message = fmt::format("`{}` must be {}", field.name, expected),
        .location = value.location,
        .label = "wrong kind of value",
        .help = {},
        .note = {},
      });
    };

    switch (field.id) {
    case FieldId::kDefault:
      ApplyDefault(declaration, type, literal, value.location);
      return;
    case FieldId::kMin:
    case FieldId::kMax:
    case FieldId::kStep: {
      if (literal.kind != Literal::Kind::kNumber) {
        mismatch("a number");
        return;
      }
      if (field.id == FieldId::kMin) {
        declaration.min = literal.number;
      } else if (field.id == FieldId::kMax) {
        declaration.max = literal.number;
      } else {
        declaration.step = literal.number;
      }
      return;
    }
    case FieldId::kUnit:
    case FieldId::kDescription:
      if (literal.kind != Literal::Kind::kString) {
        mismatch("a string");
        return;
      }
      (field.id == FieldId::kUnit ? declaration.unit : declaration.description)
        = literal.text;
      return;
    case FieldId::kOptions: {
      const auto all_strings = literal.kind == Literal::Kind::kList
        && !literal.items.empty()
        && std::ranges::all_of(literal.items, [](const Literal& item) -> bool {
             return item.kind == Literal::Kind::kString;
           });
      if (!all_strings) {
        mismatch("a non-empty list of strings");
        return;
      }
      for (const auto& item : literal.items) {
        if (item.text.size() > ScriptAnalyzer::kMaxStringValueBytes) {
          Add({
            .code = DiagnosticCode::kValueTooLong,
            .message = fmt::format("option is longer than {} bytes",
              ScriptAnalyzer::kMaxStringValueBytes),
            .location = value.location,
            .label
            = fmt::format("`{}` has {} bytes", item.text, item.text.size()),
            .help = {},
            .note = {},
          });
          return;
        }
        declaration.options.push_back(item.text);
      }
      return;
    }
    case FieldId::kColor:
      if (literal.kind != Literal::Kind::kBool) {
        mismatch("`true` or `false`");
        return;
      }
      declaration.color = literal.boolean;
      return;
    case FieldId::kKind: {
      const auto kind = literal.kind == Literal::Kind::kString
        ? std::ranges::find(kAssetKinds, literal.text, &AssetKindInfo::name)
        : kAssetKinds.end();
      if (kind == kAssetKinds.end()) {
        Add({
          .code = DiagnosticCode::kInvalidAssetKind,
          .message = "unknown asset kind",
          .location = value.location,
          .label = "not an asset kind",
          .help = "use material, geometry, scene, script, input_action, "
                  "input_mapping_context, physics_material, collision_shape "
                  "or physics_scene",
          .note = {},
        });
        return;
      }
      declaration.default_value
        = data::ScriptAssetParam { .key = {}, .asset_type = kind->type };
      return;
    }
    }
  }

  auto Extractor::ApplyDefault(ScriptParamDeclaration& declaration,
    const ParamTypeInfo& type, const Literal& literal,
    const Luau::Location& location) -> void
  {
    const auto mismatch
      = [this, &type, &location](std::string_view expected) -> void {
      Add({
        .code = DiagnosticCode::kDefaultTypeMismatch,
        .message = fmt::format(
          "default of a `{}` parameter must be {}", type.name, expected),
        .location = location,
        .label = "wrong kind of value",
        .help = {},
        .note = {},
      });
    };

    switch (type.type) {
    case ScriptParamType::kBool:
      if (literal.kind != Literal::Kind::kBool) {
        mismatch("`true` or `false`");
        return;
      }
      declaration.default_value = literal.boolean;
      return;
    case ScriptParamType::kInt32:
      if (literal.kind != Literal::Kind::kNumber
        || !IsWholeInt32(literal.number)) {
        mismatch("a whole number in the 32-bit range");
        return;
      }
      declaration.default_value = static_cast<int32_t>(literal.number);
      return;
    case ScriptParamType::kFloat:
      if (literal.kind != Literal::Kind::kNumber) {
        mismatch("a number");
        return;
      }
      declaration.default_value = static_cast<float>(literal.number);
      return;
    case ScriptParamType::kString:
      if (literal.kind != Literal::Kind::kString) {
        mismatch("a string");
        return;
      }
      if (literal.text.size() > ScriptAnalyzer::kMaxStringValueBytes) {
        Add({
          .code = DiagnosticCode::kValueTooLong,
          .message = fmt::format("string default is longer than {} bytes",
            ScriptAnalyzer::kMaxStringValueBytes),
          .location = location,
          .label = fmt::format("{} bytes", literal.text.size()),
          .help = {},
          .note = {},
        });
        return;
      }
      declaration.default_value = literal.text;
      return;
    case ScriptParamType::kVec2:
    case ScriptParamType::kVec3:
    case ScriptParamType::kVec4:
      if (!IsNumberList(literal, type.vector_size)) {
        mismatch(fmt::format("a list of {} numbers", type.vector_size));
        return;
      }
      declaration.default_value = ToVector(literal);
      return;
    case ScriptParamType::kAsset:
    case ScriptParamType::kNode:
    case ScriptParamType::kNone:
      return;
    }
  }

  auto Extractor::CheckConsistency(const ScriptParamDeclaration& declaration,
    const ParamTypeInfo& type, const Luau::AstExprCall& call,
    const std::optional<Luau::Location>& default_location) -> void
  {
    if (type.type == ScriptParamType::kAsset
      && std::get<data::ScriptAssetParam>(declaration.default_value).asset_type
        == data::AssetType::kUnknown) {
      Add({
        .code = DiagnosticCode::kInvalidAssetKind,
        .message
        = fmt::format("asset parameter `{}` needs a `kind`", declaration.name),
        .location = call.location,
        .label = "no kind",
        .help = "add the asset kind, for example `kind = \"material\"`",
        .note = {},
      });
      return;
    }

    const auto& location = default_location.value_or(call.location);
    if (declaration.min && declaration.max
      && *declaration.min > *declaration.max) {
      Add({
        .code = DiagnosticCode::kInvalidRange,
        .message = "`min` is greater than `max`",
        .location = call.argLocation,
        .label
        = fmt::format("min {} > max {}", *declaration.min, *declaration.max),
        .help = {},
        .note = {},
      });
      return;
    }
    if (declaration.step && *declaration.step <= 0.0) {
      Add({
        .code = DiagnosticCode::kInvalidRange,
        .message = "`step` must be greater than zero",
        .location = call.argLocation,
        .label = "invalid step",
        .help = {},
        .note = {},
      });
      return;
    }
    if (!default_location) {
      return;
    }

    const auto number = [&declaration] -> std::optional<double> {
      if (const auto* const value
        = std::get_if<int32_t>(&declaration.default_value)) {
        return static_cast<double>(*value);
      }
      if (const auto* const value
        = std::get_if<float>(&declaration.default_value)) {
        return static_cast<double>(*value);
      }
      return std::nullopt;
    }();
    if (number
      && ((declaration.min && *number < *declaration.min)
        || (declaration.max && *number > *declaration.max))) {
      Add({
        .code = DiagnosticCode::kInvalidRange,
        .message = "default is outside `min`..`max`",
        .location = location,
        .label = "out of range",
        .help = {},
        .note = {},
      });
      return;
    }

    const auto* const text
      = std::get_if<std::string>(&declaration.default_value);
    if (text != nullptr && !declaration.options.empty()
      && std::ranges::find(declaration.options, *text)
        == declaration.options.end()) {
      Add({
        .code = DiagnosticCode::kInvalidRange,
        .message = "default is not one of `options`",
        .location = location,
        .label = "not an option",
        .help = {},
        .note = {},
      });
    }
  }

} // namespace

auto ToSyntaxDiagnostic(const std::string_view source_name,
  const Luau::ParseError& error) -> ScriptDiagnostic
{
  return ScriptDiagnostic {
    .severity = DiagnosticSeverity::kError,
    .code = DiagnosticCode::kSyntaxError,
    .message = error.getMessage(),
    .labels = {
      DiagnosticLabel {
        .span = detail::ToSpan(source_name, error.getLocation()),
        .message = {},
        .primary = true,
      },
    },
    .notes = {},
    .help = std::nullopt,
    .slot = std::nullopt,
  };
}

auto ToSpan(const std::string_view source_name, const Luau::Location& location)
  -> SourceSpan
{
  return SourceSpan {
    .source_name = std::string(source_name),
    .begin = {
      .line = location.begin.line + 1U,
      .column = location.begin.column + 1U,
    },
    .end
    = { .line = location.end.line + 1U, .column = location.end.column + 1U },
  };
}

auto ExtractDeclarations(Luau::AstStatBlock& root,
  const std::string_view source_name,
  std::vector<ScriptDiagnostic>& diagnostics) -> ExtractedDeclarations
{
  return Extractor(source_name, diagnostics).Extract(root);
}

} // namespace oxygen::scripting::detail
