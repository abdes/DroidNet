//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <string>

#include <Oxygen/Vortex/Diagnostics/DiagnosticsFrameLedger.h>

namespace {

using oxygen::frame::SequenceNumber;
using oxygen::vortex::DiagnosticsFeature;
using oxygen::vortex::DiagnosticsFrameLedger;
using oxygen::vortex::DiagnosticsIssue;
using oxygen::vortex::DiagnosticsIssueCode;
using oxygen::vortex::DiagnosticsPassRecord;
using oxygen::vortex::DiagnosticsProductRecord;
using oxygen::vortex::DiagnosticsSeverity;
using oxygen::vortex::MakeDiagnosticsIssue;
using oxygen::vortex::ShaderDebugMode;

NOLINT_TEST(DiagnosticsFrameLedgerTest, BeginFrameResetsPreviousRecords)
{
  auto ledger = DiagnosticsFrameLedger {};
  ledger.UpdateState(ShaderDebugMode::kDirectionalShadowMask,
    DiagnosticsFeature::kFrameLedger, DiagnosticsFeature::kFrameLedger);

  ledger.BeginFrame(SequenceNumber { 1U });
  ledger.RecordPass(DiagnosticsPassRecord { .name = "A", .executed = true });
  ledger.RecordProduct(DiagnosticsProductRecord {
    .name = "P",
    .producer_pass = "A",
    .published = true,
    .valid = true,
  });
  ledger.EndFrame();

  ledger.BeginFrame(SequenceNumber { 2U });
  ledger.EndFrame();

  const auto snapshot = ledger.GetLatestSnapshot();
  EXPECT_EQ(snapshot.frame_index, SequenceNumber { 2U });
  EXPECT_TRUE(snapshot.passes.empty());
  EXPECT_TRUE(snapshot.products.empty());
  EXPECT_TRUE(snapshot.issues.empty());
  EXPECT_EQ(snapshot.active_shader_debug_mode,
    ShaderDebugMode::kDirectionalShadowMask);
}

NOLINT_TEST(DiagnosticsFrameLedgerTest, DeduplicatesIssuesWithinFrame)
{
  auto ledger = DiagnosticsFrameLedger {};
  ledger.BeginFrame(SequenceNumber { 7U });

  auto issue = MakeDiagnosticsIssue(DiagnosticsIssueCode::kMissingDebugModeProduct,
    DiagnosticsSeverity::kWarning, "first");
  issue.pass_name = "DeferredLighting";
  issue.product_name = "Vortex.DirectionalShadowMask";
  ledger.ReportIssue(issue);

  auto duplicate = MakeDiagnosticsIssue(
    DiagnosticsIssueCode::kMissingDebugModeProduct, DiagnosticsSeverity::kError,
    "second");
  duplicate.pass_name = "DeferredLighting";
  duplicate.product_name = "Vortex.DirectionalShadowMask";
  duplicate.occurrences = 2U;
  ledger.ReportIssue(duplicate);
  ledger.EndFrame();

  const auto snapshot = ledger.GetLatestSnapshot();
  ASSERT_EQ(snapshot.issues.size(), 1U);
  EXPECT_EQ(snapshot.issues[0].code, "debug-mode.missing-product");
  EXPECT_EQ(snapshot.issues[0].occurrences, 3U);
  EXPECT_EQ(snapshot.issues[0].severity, DiagnosticsSeverity::kError);
  EXPECT_EQ(snapshot.issues[0].message, "second");
}

NOLINT_TEST(DiagnosticsFrameLedgerTest, BoundsIssueContextAndCount)
{
  auto ledger = DiagnosticsFrameLedger {};
  ledger.BeginFrame(SequenceNumber { 9U });

  auto issue = MakeDiagnosticsIssue(DiagnosticsIssueCode::kStaleProduct,
    DiagnosticsSeverity::kWarning,
    std::string(DiagnosticsFrameLedger::kMaxIssueContextLength + 8U, 'x'));
  issue.view_name
    = std::string(DiagnosticsFrameLedger::kMaxIssueContextLength + 8U, 'v');
  ledger.ReportIssue(issue);

  for (auto index = 0U;
    index != DiagnosticsFrameLedger::kMaxIssuesPerFrame + 8U; ++index) {
    ledger.ReportIssue(DiagnosticsIssue {
      .severity = DiagnosticsSeverity::kWarning,
      .code = "diag.feature-unavailable",
      .message = "missing feature",
      .product_name = std::string { "Product" } + std::to_string(index),
    });
  }
  ledger.EndFrame();

  const auto snapshot = ledger.GetLatestSnapshot();
  ASSERT_FALSE(snapshot.issues.empty());
  EXPECT_EQ(snapshot.issues[0].message.size(),
    DiagnosticsFrameLedger::kMaxIssueContextLength);
  EXPECT_EQ(snapshot.issues[0].view_name.size(),
    DiagnosticsFrameLedger::kMaxIssueContextLength);
  EXPECT_EQ(snapshot.issues.size(), DiagnosticsFrameLedger::kMaxIssuesPerFrame);
}

NOLINT_TEST(DiagnosticsFrameLedgerTest, MissingDebugModeProductIsRecoverable)
{
  auto ledger = DiagnosticsFrameLedger {};
  ledger.BeginFrame(SequenceNumber { 11U });

  auto issue = MakeDiagnosticsIssue(DiagnosticsIssueCode::kMissingDebugModeProduct,
    DiagnosticsSeverity::kWarning, "debug mode requested a missing product");
  issue.pass_name = "DeferredLighting";
  issue.product_name = "Vortex.DebugDirectionalShadowMask";
  ledger.ReportIssue(issue);
  ledger.EndFrame();

  const auto snapshot = ledger.GetLatestSnapshot();
  ASSERT_EQ(snapshot.issues.size(), 1U);
  EXPECT_EQ(snapshot.issues[0].code, "debug-mode.missing-product");
  EXPECT_EQ(snapshot.issues[0].product_name,
    "Vortex.DebugDirectionalShadowMask");
}

NOLINT_TEST(DiagnosticsFrameLedgerTest, InvalidRecordContractsFailFast)
{
  auto ledger = DiagnosticsFrameLedger {};
  ledger.BeginFrame(SequenceNumber { 13U });

  NOLINT_EXPECT_DEATH(ledger.RecordPass(DiagnosticsPassRecord {}), ".*");
}

} // namespace
