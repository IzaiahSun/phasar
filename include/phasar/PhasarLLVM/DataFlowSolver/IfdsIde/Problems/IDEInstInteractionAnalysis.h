/******************************************************************************
 * Copyright (c) 2019 Philipp Schubert, Richard Leer, and Florian Sattler.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Philipp Schubert and others
 *****************************************************************************/

#ifndef PHASAR_PHASARLLVM_IFDSIDE_PROBLEMS_IDEINSTINTERACTIONALYSIS_H_
#define PHASAR_PHASARLLVM_IFDSIDE_PROBLEMS_IDEINSTINTERACTIONALYSIS_H_

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/EdgeFunctionComposer.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/EdgeFunctions.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/FlowFunctions.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/IDETabulationProblem.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/LLVMFlowFunctions.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/LLVMZeroValue.h"
#include "phasar/PhasarLLVM/Domain/AnalysisDomain.h"
#include "phasar/PhasarLLVM/Pointer/LLVMPointsToInfo.h"
#include "phasar/PhasarLLVM/TypeHierarchy/LLVMTypeHierarchy.h"
#include "phasar/PhasarLLVM/Utils/LatticeDomain.h"
#include "phasar/Utils/BitVectorSet.h"
#include "phasar/Utils/LLVMIRToSrc.h"
#include "phasar/Utils/LLVMShorthands.h"
#include "phasar/Utils/Logger.h"

// have some handy helper functionalities
namespace {

const llvm::AllocaInst *
getAllocaInstruction(const llvm::GetElementPtrInst *GEP) {
  if (!GEP) {
    return nullptr;
  }
  const auto *Alloca = GEP->getPointerOperand();
  while (const auto *NestedGEP =
             llvm::dyn_cast<llvm::GetElementPtrInst>(Alloca)) {
    Alloca = NestedGEP->getPointerOperand();
  }
  return llvm::dyn_cast<llvm::AllocaInst>(Alloca);
}

} // namespace

namespace psr {

template <typename EdgeFactType>
struct IDEInstInteractionAnalysisDomain : public LLVMAnalysisDomainDefault {
  // type of the element contained in the sets of edge functions
  using e_t = EdgeFactType;
  using l_t = LatticeDomain<BitVectorSet<e_t>>;
  using i_t = LLVMBasedICFG;
};

///
/// SyntacticAnalysisOnly: Can be set if a syntactic-only analysis is desired
/// (without using points-to information)
///
/// IndirectTaints: Can be set to ensure non-interference
///
template <typename EdgeFactType = std::string,
          bool SyntacticAnalysisOnly = false, bool EnableIndirectTaints = false>
class IDEInstInteractionAnalysisT
    : public IDETabulationProblem<
          IDEInstInteractionAnalysisDomain<EdgeFactType>> {
public:
  using AnalysisDomainTy = IDEInstInteractionAnalysisDomain<EdgeFactType>;

  using IDETabProblemType = IDETabulationProblem<AnalysisDomainTy>;
  using typename IDETabProblemType::container_type;
  using typename IDETabProblemType::FlowFunctionPtrType;
  using typename IDETabProblemType::EdgeFunctionPtrType;

  using d_t = typename AnalysisDomainTy::d_t;
  using n_t = typename AnalysisDomainTy::n_t;
  using f_t = typename AnalysisDomainTy::f_t;
  using t_t = typename AnalysisDomainTy::t_t;
  using v_t = typename AnalysisDomainTy::v_t;

  // type of the element contained in the sets of edge functions
  using e_t = typename AnalysisDomainTy::e_t;
  using l_t = typename AnalysisDomainTy::l_t;
  using i_t = typename AnalysisDomainTy::i_t;

  using EdgeFactGeneratorTy = std::set<e_t>(n_t curr);

private:
  std::function<EdgeFactGeneratorTy> edgeFactGen;
  static inline const l_t BottomElement = Bottom{};
  static inline const l_t TopElement = Top{};
  // bool GeneratedGlobalVariables = false;

  inline BitVectorSet<e_t> edgeFactGenToBitVectorSet(n_t curr) {
    if (edgeFactGen) {
      auto Results = edgeFactGen(curr);
      BitVectorSet<e_t> BVS(Results.begin(), Results.end());
      return BVS;
    }
    return {};
  }

public:
  IDEInstInteractionAnalysisT(const ProjectIRDB *IRDB,
                              const LLVMTypeHierarchy *TH,
                              const LLVMBasedICFG *ICF, LLVMPointsToInfo *PT,
                              std::set<std::string> EntryPoints = {"main"})
      : IDETabulationProblem<AnalysisDomainTy, container_type>(
            IRDB, TH, ICF, PT, std::move(EntryPoints)) {
    this->ZeroValue =
        IDEInstInteractionAnalysisT<EdgeFactType, SyntacticAnalysisOnly,
                                    EnableIndirectTaints>::createZeroValue();
  }

  ~IDEInstInteractionAnalysisT() override = default;

  // Offer a special hook to the user that allows to generate additional
  // edge facts on-the-fly. Above the generator function, the ordinary
  // edge facts are generated according to the usual edge functions.

  inline void registerEdgeFactGenerator(
      std::function<EdgeFactGeneratorTy> EdgeFactGenerator) {
    edgeFactGen = std::move(EdgeFactGenerator);
  }

  // start formulating our analysis by specifying the parts required for IFDS

  FlowFunctionPtrType getNormalFlowFunction(n_t curr, n_t succ) override {
    // generate all global variables (only once)
    // if (LLVM_UNLIKELY(!GeneratedGlobalVariables)) {
    //   if (const llvm::Module *M = curr->getModule()) {
    //     for (const auto &Global : M->globals()) {
    //       if (const auto *GV = llvm::dyn_cast<llvm::GlobalVariable>(&Global))
    //       {
    //         return std::make_shared<Gen<d_t>>(GV, this->getZeroValue());
    //       }
    //     }
    //   }
    //   GeneratedGlobalVariables = true;
    // }

    // generate all local variables as they occur
    if (const auto *Alloca = llvm::dyn_cast<llvm::AllocaInst>(curr)) {
      LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) << "AllocaInst");
      return new Gen<d_t>(Alloca, this->getZeroValue());
    }

    // handle indirect taints (propagate values that depend on branch conditions
    // whose operands are tainted)
    if (EnableIndirectTaints) {
      if (auto br = llvm::dyn_cast<llvm::BranchInst>(curr);
          br && br->isConditional()) {
        return new LambdaFlow<d_t>([=](d_t src) {
          container_type ret = {src, br};
          if (src == br->getCondition()) {
            for (auto succ : br->successors()) {
              // this->indirecrTaints[succ].insert(src);
              for (auto &inst : succ->instructionsWithoutDebug()) {
                ret.insert(&inst);
              }
            }
          }
          return ret;
        });
      }
    }

    if (!SyntacticAnalysisOnly) {
      // (ii) handle semantic propagation (pointers)
      if (const auto *Load = llvm::dyn_cast<llvm::LoadInst>(curr)) {
        // if one of the potentially many loaded values holds, the load itself
        // must also be populated
        struct IIAFlowFunction : FlowFunction<d_t, container_type> {
          IDEInstInteractionAnalysisT &Problem;
          const llvm::LoadInst *Load;
          std::unordered_set<d_t> PTS;

          IIAFlowFunction(IDEInstInteractionAnalysisT &Problem,
                          const llvm::LoadInst *Load)
              : Problem(Problem), Load(Load),
                PTS(*Problem.PT->getPointsToSet(Load->getPointerOperand())) {}

          container_type computeTargets(d_t src) override {
            container_type Facts;
            Facts.insert(src);
            if (PTS.count(src)) {
              Facts.insert(Load);
            }
            return Facts;
          }
        };
        return new IIAFlowFunction(*this, Load);
      }

      // (ii) handle semantic propagation (pointers)
      if (const auto *Store = llvm::dyn_cast<llvm::StoreInst>(curr)) {
        // if the value to be stored holds the potentially memory location
        // that it is stored to must be populated as well
        struct IIAFlowFunction : FlowFunction<d_t, container_type> {
          IDEInstInteractionAnalysisT &Problem;
          const llvm::StoreInst *Store;
          std::unordered_set<d_t> ValuePTS;
          std::unordered_set<d_t> PointerPTS;

          IIAFlowFunction(IDEInstInteractionAnalysisT &Problem,
                          const llvm::StoreInst *Store)
              : Problem(Problem), Store(Store),
                ValuePTS(*Problem.PT->getPointsToSet(Store->getValueOperand())),
                PointerPTS(
                    *Problem.PT->getPointsToSet(Store->getPointerOperand())) {}

          container_type computeTargets(d_t src) override {
            container_type Facts;
            Facts.insert(src);
            // if a value is stored that holds we must generate all potential
            // memory locations the store might write to
            if (Store->getValueOperand() == src || ValuePTS.count(src)) {
              Facts.insert(Store->getPointerOperand());
              Facts.insert(PointerPTS.begin(), PointerPTS.end());
            }
            // if the value to be stored does not hold then we must at least add
            // the store instruction and the points-to set as the instruction
            // still interacts with the memory locations pointed to be PTS
            if (Store->getPointerOperand() == src || PointerPTS.count(src)) {
              Facts.insert(Store);
              Facts.erase(src);
            }
            return Facts;
          }
        };
        return new IIAFlowFunction(*this, Store);
      }
    }

    // (i) handle syntactic propagation store instructions
    // in case store x y, we need to draw the edge x --> y such that we can
    // transfer x's labels to y
    if (const auto *Store = llvm::dyn_cast<llvm::StoreInst>(curr)) {
      if (const auto *Load =
              llvm::dyn_cast<llvm::LoadInst>(Store->getValueOperand())) {
        struct IIAAFlowFunction : FlowFunction<d_t> {
          const llvm::StoreInst *Store;
          const llvm::LoadInst *Load;
          IIAAFlowFunction(const llvm::StoreInst *S, const llvm::LoadInst *L)
              : Store(S), Load(L) {}
          ~IIAAFlowFunction() override = default;

          container_type computeTargets(d_t src) override {
            container_type Facts;
            if (Load == src || Load->getPointerOperand() == src) {
              Facts.insert(src);
              Facts.insert(Load->getPointerOperand());
              Facts.insert(Store->getPointerOperand());
            } else {
              Facts.insert(src);
            }
            for (const auto s : Facts) {
              LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                            << "Create edge: " << llvmIRToShortString(src)
                            << " --" << llvmIRToShortString(Store) << "--> "
                            << llvmIRToShortString(s));
            }
            return Facts;
          }
        };
        return new IIAAFlowFunction(Store, Load);
      } else {
        struct IIAAFlowFunction : FlowFunction<d_t> {
          const llvm::StoreInst *Store;
          IIAAFlowFunction(const llvm::StoreInst *S) : Store(S) {}
          ~IIAAFlowFunction() override = default;

          container_type computeTargets(d_t src) override {
            container_type Facts;
            if (Store->getValueOperand() == src) {
              Facts.insert(src);
              Facts.insert(Store->getPointerOperand());
            } else {
              Facts.insert(src);
            }
            for (const auto s : Facts) {
              LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                            << "Create edge: " << llvmIRToShortString(src)
                            << " --" << llvmIRToShortString(Store) << "--> "
                            << llvmIRToShortString(s));
            }
            return Facts;
          }
        };
        return new IIAAFlowFunction(Store);
      }
    }
    // and now we can handle all other statements
    struct IIAFlowFunction : FlowFunction<d_t> {
      IDEInstInteractionAnalysisT &Problem;
      n_t Inst;

      IIAFlowFunction(IDEInstInteractionAnalysisT &Problem, n_t Inst)
          : Problem(Problem), Inst(Inst) {}

      ~IIAFlowFunction() override = default;

      container_type computeTargets(d_t src) override {
        container_type Facts;
        if (Problem.isZeroValue(src)) {
          // keep the zero flow fact
          Facts.insert(src);
          return Facts;
        }
        // (i) syntactic propagation
        if (Inst == src) {
          Facts.insert(Inst);
        }
        // continue syntactic propagation: populate and propagate other existing
        // facts
        for (auto &Op : Inst->operands()) {
          // if one of the operands holds, also generate the instruction using
          // it
          if (Op == src) {
            Facts.insert(Inst);
            Facts.insert(src);
          }
        }
        // pass everything that already holds as identity
        Facts.insert(src);
        for (const auto s : Facts) {
          LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                        << "Create edge: " << llvmIRToShortString(src) << " --"
                        << llvmIRToShortString(Inst) << "--> "
                        << llvmIRToShortString(s));
        }
        return Facts;
      }
    };
    return new IIAFlowFunction(*this, curr);
  }

  inline FlowFunctionPtrType getCallFlowFunction(n_t callStmt,
                                                 f_t destMthd) override {
    // just use the auto mapping
    return new MapFactsToCallee<container_type>(
        llvm::ImmutableCallSite(callStmt), destMthd);
  }

  inline FlowFunctionPtrType getRetFlowFunction(n_t callSite, f_t calleeMthd,
                                                n_t exitStmt,
                                                n_t retSite) override {
    // if pointer parameters hold at the end of a callee function generate all
    // of the
    return new MapFactsToCaller<container_type>(
        llvm::ImmutableCallSite(callSite), calleeMthd, exitStmt);
  }

  inline FlowFunctionPtrType
  getCallToRetFlowFunction(n_t callSite, n_t retSite,
                           std::set<f_t> callees) override {
    // just use the auto mapping, pointer parameters are killed and handled by
    // getCallFlowfunction() and getRetFlowFunction()
    return new MapFactsAlongsideCallSite<container_type>(
        llvm::ImmutableCallSite(callSite));
  }

  inline FlowFunctionPtrType getSummaryFlowFunction(n_t callStmt,
                                                    f_t destMthd) override {
    // do not use user-crafted summaries
    return nullptr;
  }

  inline std::map<n_t, container_type> initialSeeds() override {
    std::map<n_t, container_type> SeedMap;
    for (auto &EntryPoint : this->EntryPoints) {
      SeedMap.insert(
          std::make_pair(&this->ICF->getFunction(EntryPoint)->front().front(),
                         container_type({this->getZeroValue()})));
    }
    return SeedMap;
  }

  [[nodiscard]] inline d_t createZeroValue() const override {
    // create a special value to represent the zero value!
    return LLVMZeroValue::getInstance();
  }

  inline bool isZeroValue(d_t d) const override {
    return LLVMZeroValue::getInstance()->isLLVMZeroValue(d);
  }

  // in addition provide specifications for the IDE parts

  inline EdgeFunctionPtrType
  getNormalEdgeFunction(n_t curr, d_t currNode, n_t succ,
                        d_t succNode) override {
    LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                  << "Process edge: " << llvmIRToShortString(currNode) << " --"
                  << llvmIRToShortString(curr) << "--> "
                  << llvmIRToShortString(succNode));

    // propagate zero edges as identity
    if (isZeroValue(currNode) && isZeroValue(succNode)) {
      return EdgeIdentity<l_t>::getInstance();
    }

    // check if the user has registered a fact generator function
    l_t UserEdgeFacts;
    std::set<e_t> EdgeFacts;
    if (edgeFactGen) {
      EdgeFacts = edgeFactGen(curr);
      // fill BitVectorSet
      UserEdgeFacts = BitVectorSet<e_t>(EdgeFacts.begin(), EdgeFacts.end());
    }

    // override at store instructions
    if (const auto *Store = llvm::dyn_cast<llvm::StoreInst>(curr)) {
      if (SyntacticAnalysisOnly) {
        // check for the overriding edges at store instructions
        // store x y
        // case x and y ordinary variables
        // y obtains its values from x (and from the store itself)
        if (const auto *Load =
                llvm::dyn_cast<llvm::LoadInst>(Store->getValueOperand())) {
          if (Load->getPointerOperand() == currNode &&
              succNode == Store->getPointerOperand()) {
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                          << "Var-Override: ");
            for (const auto &EF : EdgeFacts) {
              LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) << EF << ", ");
            }
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                          << "at '" << llvmIRToString(curr) << "'\n");
            return new IIAAKillOrReplaceEF(*this, UserEdgeFacts);
          }
        }
        // kill all labels that are propagated along the edge of the value that
        // is overridden
        if ((currNode == succNode) &&
            (currNode == Store->getPointerOperand())) {
          if (llvm::isa<llvm::ConstantData>(Store->getValueOperand())) {
            // case x is a literal (and y an ordinary variable)
            // y obtains its values from its original allocation and this store
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                          << "Const-Replace at '" << llvmIRToString(curr)
                          << "'\n");
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                          << "Replacement label(s): ");
            for (const auto &Item : EdgeFacts) {
              LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) << Item << ", ");
            }
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) << '\n');
            // obtain label from the original allocation
            const llvm::AllocaInst *OrigAlloca = nullptr;
            if (const auto *Alloca = llvm::dyn_cast<llvm::AllocaInst>(
                    Store->getPointerOperand())) {
              OrigAlloca = Alloca;
            }
            if (const auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(
                    Store->getPointerOperand())) {
              OrigAlloca = getAllocaInstruction(GEP);
            }
            // obtain the label
            if (OrigAlloca) {
              if (auto *UEF = std::get_if<BitVectorSet<e_t>>(&UserEdgeFacts)) {
                UEF->insert(edgeFactGenToBitVectorSet(OrigAlloca));
              }
            }
            return new IIAAKillOrReplaceEF(*this, UserEdgeFacts);
          } else {
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                          << "Kill at '" << llvmIRToString(curr) << "'\n");
            // obtain label from original allocation and add it
            const llvm::AllocaInst *OrigAlloca = nullptr;
            if (const auto *Alloca = llvm::dyn_cast<llvm::AllocaInst>(
                    Store->getPointerOperand())) {
              OrigAlloca = Alloca;
            }
            if (const auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(
                    Store->getPointerOperand())) {
              OrigAlloca = getAllocaInstruction(GEP);
            }
            // obtain the label
            if (OrigAlloca) {
              if (auto *UEF = std::get_if<BitVectorSet<e_t>>(&UserEdgeFacts)) {
                UEF->insert(edgeFactGenToBitVectorSet(OrigAlloca));
              }
            }
            return new IIAAKillOrReplaceEF(*this, UserEdgeFacts);
          }
        }
      } else {
        // consider points-to information and find all possible overriding edges
        // using points-to sets
        std::shared_ptr<std::unordered_set<d_t>> ValuePTS;
        if (Store->getValueOperand()->getType()->isPointerTy()) {
          ValuePTS = this->PT->getPointsToSet(Store->getValueOperand());
        }
        auto PointerPTS = this->PT->getPointsToSet(Store->getPointerOperand());
        // overriding edge
        if ((currNode == Store->getValueOperand() ||
             ValuePTS->count(Store->getValueOperand()) ||
             llvm::isa<llvm::ConstantData>(Store->getValueOperand())) &&
            PointerPTS->count(Store->getPointerOperand())) {
          return new IIAAKillOrReplaceEF(*this, UserEdgeFacts);
        }
        // kill all labels that are propagated along the edge of the
        // value/values that is/are overridden
        if (currNode == succNode && PointerPTS->count(currNode)) {
          return new IIAAKillOrReplaceEF(*this);
        }
      }
    }

    // check if the user has registered a fact generator function
    if (auto UEF = std::get_if<BitVectorSet<e_t>>(&UserEdgeFacts)) {
      if (!UEF->empty()) {
        // handle generating edges from zero
        // generate labels from zero when the instruction itself is the flow
        // fact that is generated
        if (isZeroValue(currNode) && curr == succNode) {
          return new IIAAAddLabelsEF(*this, UserEdgeFacts);
        }
        // handle edges that may add new labels to existing facts
        if (curr == currNode && currNode == succNode) {
          return new IIAAAddLabelsEF(*this, UserEdgeFacts);
        }
        // generate labels from zero when an operand of the current instruction
        // is a flow fact that is generated
        for (const auto &Op : curr->operands()) {
          // also propagate the labels if one of the operands holds
          if (isZeroValue(currNode) && Op == succNode) {
            return new IIAAAddLabelsEF(*this, UserEdgeFacts);
          }
          // handle edges that may add new labels to existing facts
          if (Op == currNode && currNode == succNode) {
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                          << "this is 'i'\n");
            for (auto &EdgeFact : EdgeFacts) {
              LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                            << EdgeFact << ", ");
            }
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) << '\n');
            return new IIAAAddLabelsEF(*this, UserEdgeFacts);
          }
          // handle edge that are drawn from existing facts
          if (Op == currNode && curr == succNode) {
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                          << "this is '0'\n");
            for (auto &EdgeFact : EdgeFacts) {
              LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                            << EdgeFact << ", ");
            }
            LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) << '\n');
            return new IIAAAddLabelsEF(*this, UserEdgeFacts);
          }
        }
      }
    }
    // otherwise stick to identity
    return EdgeIdentity<l_t>::getInstance();
  }

  inline EdgeFunctionPtrType
  getCallEdgeFunction(n_t callStmt, d_t srcNode, f_t destinationMethod,
                      d_t destNode) override {
    // can be passed as identity
    return EdgeIdentity<l_t>::getInstance();
  }

  inline EdgeFunctionPtrType
  getReturnEdgeFunction(n_t callSite, f_t calleeMethod, n_t exitStmt,
                        d_t exitNode, n_t reSite, d_t retNode) override {
    // can be passed as identity
    return EdgeIdentity<l_t>::getInstance();
  }

  inline EdgeFunctionPtrType
  getCallToRetEdgeFunction(n_t callSite, d_t callNode, n_t retSite,
                           d_t retSiteNode, std::set<f_t> callees) override {
    // just forward to getNormalEdgeFunction() to check whether a user has
    // additional labels for this call site
    return getNormalEdgeFunction(callSite, callNode, retSite, retSiteNode);
  }

  inline EdgeFunctionPtrType
  getSummaryEdgeFunction(n_t callSite, d_t callNode, n_t retSite,
                         d_t retSiteNode) override {
    // do not use user-crafted summaries
    return nullptr;
  }

  inline l_t topElement() override { return TopElement; }

  inline l_t bottomElement() override { return BottomElement; }

  inline l_t join(l_t Lhs, l_t Rhs) override {
    if (Lhs == BottomElement || Rhs == BottomElement) {
      return BottomElement;
    }
    if (Lhs == TopElement) {
      return Rhs;
    }
    if (Rhs == TopElement) {
      return Lhs;
    }
    auto LhsSet = std::get<BitVectorSet<e_t>>(Lhs);
    auto RhsSet = std::get<BitVectorSet<e_t>>(Rhs);
    return LhsSet.setUnion(RhsSet);
  }

  inline EdgeFunctionPtrType allTopFunction() override {
    return new AllTop<l_t>(topElement());
  }

  // provide some handy helper edge functions to improve reuse

  // edge function that kills all labels in a set (and may replaces them with
  // others)
  class IIAAKillOrReplaceEF
      : public EdgeFunction<l_t> {
  private:
    IDEInstInteractionAnalysisT<e_t, SyntacticAnalysisOnly,
                                EnableIndirectTaints> &Analysis;

  public:
    l_t Replacement;

    explicit IIAAKillOrReplaceEF(
        IDEInstInteractionAnalysisT<e_t, SyntacticAnalysisOnly,
                                    EnableIndirectTaints> &Analysis)
        : Analysis(Analysis), Replacement(BitVectorSet<e_t>()) {
      LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                    << "IIAAKillOrReplaceEF");
    }

    explicit IIAAKillOrReplaceEF(
        IDEInstInteractionAnalysisT<e_t, SyntacticAnalysisOnly,
                                    EnableIndirectTaints> &Analysis,
        l_t Replacement)
        : Analysis(Analysis), Replacement(Replacement) {
      LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                    << "IIAAKillOrReplaceEF");
    }

    ~IIAAKillOrReplaceEF() override = default;

    l_t computeTarget(l_t Src) override { return Replacement; }

    EdgeFunctionPtrType
    composeWith(EdgeFunctionPtrType secondFunction, MemoryManager<AnalysisDomainTy, Container> memManager) override {
      LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                    << "IIAAKillOrReplaceEF::composeWith(): " << this->str()
                    << " * " << secondFunction->str());
      // kill or replace, previous functions are ignored
      if (auto *KR =
              dynamic_cast<IIAAKillOrReplaceEF *>(secondFunction)) {
        if (KR->isKillAll()) {
          return secondFunction;
        }
      }
      return this;
    }

    EdgeFunctionPtrType
    joinWith(EdgeFunctionPtrType otherFunction) override {
      // LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) <<
      // "IIAAKillOrReplaceEF::joinWith");
      if (auto *AB = dynamic_cast<AllBottom<l_t> *>(otherFunction)) {
        return this;
      }
      if (auto *ID = dynamic_cast<EdgeIdentity<l_t> *>(otherFunction)) {
        return this;
      }
      if (auto *AD = dynamic_cast<IIAAAddLabelsEF *>(otherFunction)) {
        return this;
      }
      if (auto *KR = dynamic_cast<IIAAKillOrReplaceEF *>(otherFunction)) {
        Replacement = Analysis.join(Replacement, KR->Replacement);
        return this;
      }
      llvm::report_fatal_error(
          "found unexpected edge function in 'IIAAKillOrReplaceEF'");
    }

    bool equal_to(EdgeFunctionPtrType other) const override {
      // LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) <<
      // "IIAAKillOrReplaceEF::equal_to");
      if (auto *I = dynamic_cast<IIAAKillOrReplaceEF *>(other)) {
        return Replacement == I->Replacement;
      }
      return this == other;
    }

    void print(std::ostream &OS, bool isForDebug = false) const override {
      OS << "EF: (IIAAKillOrReplaceEF)<->";
      if (isKillAll()) {
        OS << "(KillAll";
      } else {
        Analysis.printEdgeFact(OS, Replacement);
      }
      OS << ")";
    }

    bool isKillAll() const {
      if (auto *RSet = std::get_if<BitVectorSet<e_t>>(&Replacement)) {
        return RSet->empty();
      }
      return false;
    }
  };

  // edge function that adds the given labels to existing labels
  // add all labels provided by 'Data'
  class IIAAAddLabelsEF : public EdgeFunction<l_t>,
                          public std::enable_shared_from_this<IIAAAddLabelsEF> {
  private:
    IDEInstInteractionAnalysisT<e_t, SyntacticAnalysisOnly,
                                EnableIndirectTaints> &Analysis;

  public:
    l_t Data;

    explicit IIAAAddLabelsEF(
        IDEInstInteractionAnalysisT<e_t, SyntacticAnalysisOnly,
                                    EnableIndirectTaints> &Analysis,
        l_t Data)
        : Analysis(Analysis), Data(Data) {
      LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) << "IIAAAddLabelsEF");
    }

    ~IIAAAddLabelsEF() override = default;

    l_t computeTarget(l_t Src) override { return Analysis.join(Src, Data); }

    EdgeFunctionPtrType
    composeWith(EdgeFunctionPtrType secondFunction, MemoryManager<AnalysisDomainTy, Container> memManager) override {
      LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG)
                    << "IIAAAddLabelEF::composeWith(): " << this->str() << " * "
                    << secondFunction->str());
      if (auto *AB = dynamic_cast<AllBottom<l_t> *>(secondFunction)) {
        return this;
      }
      if (auto *EI = dynamic_cast<EdgeIdentity<l_t> *>(secondFunction)) {
        return this;
      }
      if (auto *AS = dynamic_cast<IIAAAddLabelsEF *>(secondFunction)) {
        auto Union = Analysis.join(Data, AS->Data);
        return new IIAAAddLabelsEF(Analysis, Union);
      }
      if (auto *KR =
              dynamic_cast<IIAAKillOrReplaceEF *>(secondFunction)) {
        return new IIAAAddLabelsEF(Analysis, KR->Replacement);
      }
      llvm::report_fatal_error(
          "found unexpected edge function in 'IIAAAddLabelsEF'");
    }

    EdgeFunctionPtrType
    joinWith(EdgeFunctionPtrType otherFunction) override {
      // LOG_IF_ENABLE(BOOST_LOG_SEV(lg::get(), DFADEBUG) <<
      // "IIAAAddLabelsEF::joinWith");
      if (otherFunction == this ||
          otherFunction->equal_to(this)) {
        return this;
      }
      if (auto *AT = dynamic_cast<AllTop<l_t> *>(otherFunction)) {
        return this;
      }
      if (auto *AS = dynamic_cast<IIAAAddLabelsEF *>(otherFunction)) {
        auto Union = Analysis.join(Data, AS->Data);
        return new IIAAAddLabelsEF(Analysis, Union);
      }
      if (auto *KR = dynamic_cast<IIAAKillOrReplaceEF *>(otherFunction)) {
        auto Union = Analysis.join(Data, KR->Replacement);
        return new IIAAAddLabelsEF(Analysis, Union);
      }
      return new AllBottom<l_t>(Analysis.BottomElement);
    }

    [[nodiscard]] bool
    equal_to(EdgeFunctionPtrType other) const override {
      // std::cout << "IIAAAddLabelsEF::equal_to\n";
      if (auto *I = dynamic_cast<IIAAAddLabelsEF *>(other)) {
        return (I->Data == this->Data);
      }
      return this == other;
    }

    void print(std::ostream &OS, bool isForDebug = false) const override {
      OS << "EF: (IIAAAddLabelsEF: ";
      Analysis.printEdgeFact(OS, Data);
      OS << ")";
    }
  };

  // provide functionalities for printing things and emitting text reports

  void printNode(std::ostream &os, n_t n) const override {
    os << llvmIRToString(n);
  }

  void printDataFlowFact(std::ostream &os, d_t d) const override {
    os << llvmIRToString(d);
  }

  void printFunction(std::ostream &os, f_t m) const override {
    os << m->getName().str();
  }

  void printEdgeFact(std::ostream &os, l_t l) const override {
    if (std::holds_alternative<Top>(l)) {
      os << std::get<Top>(l);
    } else if (std::holds_alternative<Bottom>(l)) {
      os << std::get<Bottom>(l);
    } else {
      auto lset = std::get<BitVectorSet<e_t>>(l);
      os << "(set size: " << lset.size() << "), values: ";
      size_t idx = 0;
      for (const auto &s : lset) {
        os << s;
        if (idx != lset.size() - 1) {
          os << ", ";
        }
        ++idx;
      }
    }
  }

  void stripBottomResults(std::unordered_map<d_t, l_t> &Res) {
    for (auto it = Res.begin(); it != Res.end();) {
      if (it->second == BottomElement) {
        it = Res.erase(it);
      } else {
        ++it;
      }
    }
  }

  void emitTextReport(const SolverResults<n_t, d_t, l_t> &SR,
                      std::ostream &OS = std::cout) override {
    OS << "\n====================== IDE-Inst-Interaction-Analysis Report "
          "======================\n";
    // if (!IRDB->debugInfoAvailable()) {
    //   // Emit only IR code, function name and module info
    //   OS << "\nWARNING: No Debug Info available - emiting results without "
    //         "source code mapping!\n";
    for (const auto *f : this->ICF->getAllFunctions()) {
      std::string fName = getFunctionNameFromIR(f);
      OS << "\nFunction: " << fName << "\n----------"
         << std::string(fName.size(), '-') << '\n';
      for (const auto *stmt : this->ICF->getAllInstructionsOf(f)) {
        auto results = SR.resultsAt(stmt, true);
        stripBottomResults(results);
        if (!results.empty()) {
          OS << "At IR statement: " << this->NtoString(stmt) << '\n';
          for (auto res : results) {
            if (res.second != BottomElement) {
              OS << "   Fact: " << this->DtoString(res.first)
                 << "\n  Value: " << this->LtoString(res.second) << '\n';
            }
          }
          OS << '\n';
        }
      }
      OS << '\n';
    }
    // } else {
    // TODO: implement better report in case debug information are available
    //   }
  }
}; // namespace psr

using IDEInstInteractionAnalysis = IDEInstInteractionAnalysisT<>;

} // namespace psr

#endif
