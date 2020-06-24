#ifndef SRC_ANALYSIS_PLUGINS_IDETABULATIONPROBLEMTESTPLUGIN_H_
#define SRC_ANALYSIS_PLUGINS_IDETABULATIONPROBLEMTESTPLUGIN_H_

#include "phasar/PhasarLLVM/Plugins/Interfaces/IfdsIde/IDETabulationProblemPlugin.h"

namespace psr {

class IDETabulationProblemTestPlugin : public IDETabulationProblemPlugin {
public:
  IDETabulationProblemTestPlugin(const ProjectIRDB *IRDB,
                                 const LLVMTypeHierarchy *TH,
                                 const LLVMBasedICFG *ICF, LLVMPointsToInfo *PT,
                                 std::set<std::string> EntryPoints);

  ~IDETabulationProblemTestPlugin() = default;

  FlowFunctionPtrType
  getNormalFlowFunction(const llvm::Instruction *curr,
                        const llvm::Instruction *succ) override;

  FlowFunctionPtrType
  getCallFlowFunction(const llvm::Instruction *callStmt,
                      const llvm::Function *destFun) override;

  FlowFunctionPtrType
  getRetFlowFunction(const llvm::Instruction *callSite,
                     const llvm::Function *calleeFun,
                     const llvm::Instruction *exitStmt,
                     const llvm::Instruction *retSite) override;

  FlowFunctionPtrType
  getCallToRetFlowFunction(const llvm::Instruction *callSite,
                           const llvm::Instruction *retSite,
                           std::set<const llvm::Function *> callees) override;

  FlowFunctionPtrType
  getSummaryFlowFunction(const llvm::Instruction *callStmt,
                         const llvm::Function *destFun) override;

  std::map<const llvm::Instruction *, std::set<const llvm::Value *>>
  initialSeeds() override;

  EdgeFunctionPtrType
  getNormalEdgeFunction(n_t curr, d_t currNode, n_t succ,
                        d_t succNode) override;
  EdgeFunctionPtrType
  getCallEdgeFunction(n_t callStmt, d_t srcNode, f_t destinationFunction,
                      d_t destNode) override;
  EdgeFunctionPtrType
  getReturnEdgeFunction(n_t callSite, f_t calleeFunction, n_t exitStmt,
                        d_t exitNode, n_t reSite, d_t retNode) override;
  EdgeFunctionPtrType
  getCallToRetEdgeFunction(n_t callSite, d_t callNode, n_t retSite,
                           d_t retSiteNode, std::set<f_t> callees) override;
  EdgeFunctionPtrType
  getSummaryEdgeFunction(n_t curr, d_t currNode, n_t succ,
                         d_t succNode) override;

  l_t topElement() override;
  l_t bottomElement() override;
  l_t join(l_t lhs, l_t rhs) override;
};

extern "C" std::unique_ptr<IDETabulationProblemPlugin>
makeIDETabulationProblemtestPlugin(const ProjectIRDB *IRDB,
                                   const LLVMTypeHierarchy *TH,
                                   const LLVMBasedICFG *ICF,
                                   LLVMPointsToInfo *PT,
                                   std::set<std::string> EntryPoints);

} // namespace psr
#endif