/******************************************************************************
 * Copyright (c) 2017 Philipp Schubert.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Philipp Schubert and others
 *****************************************************************************/

#ifndef PHASAR_PHASARLLVM_IFDSIDE_PROBLEMS_IDESOLVERTEST_H_
#define PHASAR_PHASARLLVM_IFDSIDE_PROBLEMS_IDESOLVERTEST_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/IDETabulationProblem.h"

namespace llvm {
class Instruction;
class Function;
class StructType;
class Value;
} // namespace llvm

namespace psr {

class LLVMBasedICFG;
class LLVMTypeHierarchy;
class LLVMPointsToInfo;

class IDESolverTest
    : public IDETabulationProblem<const llvm::Instruction *,
                                  const llvm::Value *, const llvm::Function *,
                                  const llvm::StructType *, const llvm::Value *,
                                  const llvm::Value *, LLVMBasedICFG> {
private:
  std::vector<std::string> EntryPoints;

public:
  typedef const llvm::Value *d_t;
  typedef const llvm::Instruction *n_t;
  typedef const llvm::Function *f_t;
  typedef const llvm::StructType *t_t;
  typedef const llvm::Value *v_t;
  typedef const llvm::Value *l_t;
  typedef LLVMBasedICFG i_t;

  IDESolverTest(const ProjectIRDB *IRDB, const LLVMTypeHierarchy *TH,
                const LLVMBasedICFG *ICF, const LLVMPointsToInfo *PT,
                std::set<std::string> EntryPoints = {"main"});

  ~IDESolverTest() override = default;

  // start formulating our analysis by specifying the parts required for IFDS

  FlowFunction<d_t> *getNormalFlowFunction(n_t curr, n_t succ) override;

  FlowFunction<d_t> *getCallFlowFunction(n_t callStmt, f_t destFun) override;

  FlowFunction<d_t> *getRetFlowFunction(n_t callSite, f_t calleeFun,
                                        n_t exitStmt, n_t retSite) override;

  FlowFunction<d_t> *getCallToRetFlowFunction(n_t callSite, n_t retSite,
                                              std::set<f_t> callees) override;

  FlowFunction<d_t> *getSummaryFlowFunction(n_t callStmt, f_t destFun) override;

  std::map<n_t, std::set<d_t>> initialSeeds() override;

  d_t createZeroValue() const override;

  bool isZeroValue(d_t d) const override;

  // in addition provide specifications for the IDE parts

  EdgeFunction<l_t> *getNormalEdgeFunction(n_t curr, d_t currNode, n_t succ,
                                           d_t succNode) override;

  EdgeFunction<l_t> *getCallEdgeFunction(n_t callStmt, d_t srcNode,
                                         f_t destinationFunction,
                                         d_t destNode) override;

  EdgeFunction<l_t> *getReturnEdgeFunction(n_t callSite, f_t calleeFunction,
                                           n_t exitStmt, d_t exitNode,
                                           n_t reSite, d_t retNode) override;

  EdgeFunction<l_t> *getCallToRetEdgeFunction(n_t callSite, d_t callNode,
                                              n_t retSite, d_t retSiteNode,
                                              std::set<f_t> callees) override;

  EdgeFunction<l_t> *getSummaryEdgeFunction(n_t callStmt, d_t callNode,
                                            n_t retSite,
                                            d_t retSiteNode) override;

  l_t topElement() override;

  l_t bottomElement() override;

  l_t join(l_t lhs, l_t rhs) override;

  EdgeFunction<l_t> *allTopFunction() override;

  class IDESolverTestAllTop : public EdgeFunction<l_t> {
    l_t computeTarget(l_t source) override;

    EdgeFunction<l_t> *composeWith(EdgeFunction<l_t> *secondFunction) override;

    EdgeFunction<l_t> *joinWith(EdgeFunction<l_t> *otherFunction) override;

    bool equal_to(EdgeFunction<l_t> *other) const override;
  };

  void printNode(std::ostream &os, n_t n) const override;

  void printDataFlowFact(std::ostream &os, d_t d) const override;

  void printFunction(std::ostream &os, f_t m) const override;

  void printEdgeFact(std::ostream &os, l_t l) const override;
};

} // namespace psr

#endif
