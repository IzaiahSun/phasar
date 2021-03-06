/******************************************************************************
 * Copyright (c) 2017 Philipp Schubert.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Philipp Schubert and others
 *****************************************************************************/

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h" // clang::tooling::ClangTool, clang::tooling::newFrontendActionFactory

#include "phasar/PhasarClang/RandomChangeFrontendAction.h"

#include "phasar/PhasarClang/ClangController.h"
#include "phasar/Utils/Logger.h"

using namespace std;
using namespace psr;

namespace psr {

ClangController::ClangController(
    clang::tooling::CommonOptionsParser &OptionsParser) {
  auto &lg = lg::get();
  LOG_IF_ENABLE(BOOST_LOG_SEV(lg, DEBUG)
                << "ClangController::ClangController()");
  LOG_IF_ENABLE(BOOST_LOG_SEV(lg, DEBUG) << "Source file(s):");
  // for (auto &src : OptionsParser.getSourcePathList()) {
  for (auto src : OptionsParser.getCompilations().getAllFiles()) {
    LOG_IF_ENABLE(BOOST_LOG_SEV(lg, DEBUG) << src);
  }
  clang::tooling::ClangTool Tool(OptionsParser.getCompilations(),
                                 OptionsParser.getCompilations().getAllFiles());
  int result = Tool.run(
      clang::tooling::newFrontendActionFactory<RandomChangeFrontendAction>()
          .get());
  LOG_IF_ENABLE(BOOST_LOG_SEV(lg, DEBUG) << "finished clang ast analysis.");
}
} // namespace psr
