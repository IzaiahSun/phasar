/******************************************************************************
 * Copyright (c) 2017 Philipp Schubert.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Philipp Schubert and others
 *****************************************************************************/

#ifndef PHASAR_PHASARLLVM_IFDSIDE_EDGEFUNCTIONCOMPOSER_H
#define PHASAR_PHASARLLVM_IFDSIDE_EDGEFUNCTIONCOMPOSER_H

#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/EdgeFunction.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/EdgeFunctions/AllBottom.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/EdgeFunctions/EdgeIdentity.h"
#include <memory>

namespace psr {

/**
 * This abstract class models edge function composition. It holds two edge
 * functions. The edge function computation order is implemented as
 *  F -> G -> otherFunction
 * i.e. F is computed before G, G is computed before otherFunction.
 *
 * Note that an own implementation for the join function is required, since
 * this varies between different analyses, and is not implemented by this
 * class.
 * It is also advised to provide a more precise compose function, which is able
 * to reduce the result of the composition, rather than using the default
 * implementation. By default, an explicit composition is used. Such a function
 * definition can grow unduly large.
 */
template <typename L> class EdgeFunctionComposer : public EdgeFunction<L> {
private:
  // For debug purpose only
  const unsigned EFComposer_Id;
  static inline unsigned CurrEFComposer_Id = 0;

protected:
  /// First edge function
  EdgeFunction<L> *F;
  /// Second edge function
  EdgeFunction<L> *G;

public:
  EdgeFunctionComposer(EdgeFunction<L> *F, EdgeFunction<L> *G)
      : EFComposer_Id(++CurrEFComposer_Id), F(F), G(G) {}

  ~EdgeFunctionComposer() override = default;

  /**
   * Target value computation is implemented as
   *     G(F(source))
   */
  L computeTarget(L source) override {
    return G->computeTarget(F->computeTarget(source));
  }

  /**
   * Function composition is implemented as an explicit composition, i.e.
   *     (secondFunction * G) * F = EFC(F, EFC(G , otherFunction))
   *
   * However, it is advised to immediately reduce the resulting edge function
   * by providing an own implementation of this function.
   */
  EdgeFunction<L> *composeWith(EdgeFunction<L> *secondFunction) override {
    if (auto *EI = dynamic_cast<EdgeIdentity<L> *>(secondFunction)) {
      return this;
    }
    if (auto *AB = dynamic_cast<AllBottom<L> *>(secondFunction)) {
      return this;
    }
    return F->composeWith(G->composeWith(secondFunction));
  }

  // virtual EdgeFunction<L> *
  // joinWith(EdgeFunction<L> * otherFunction) = 0;

  bool equal_to(EdgeFunction<L> *other) const override {
    if (auto EFC = dynamic_cast<EdgeFunctionComposer<L> *>(other)) {
      return F->equal_to(EFC->F) && G->equal_to(EFC->G);
    }
    return false;
  }

  void print(std::ostream &OS, bool isForDebug = false) const override {
    OS << "COMP[ " << F->str() << " , " << G->str()
       << " ] (EF:" << EFComposer_Id << ')';
  }
};

} // namespace psr

#endif
