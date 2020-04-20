/******************************************************************************
 * Copyright (c) 2017 Philipp Schubert.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Philipp Schubert and others
 *****************************************************************************/

#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/Solver/IFDSToIDETabulationProblem.h"
#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/EdgeFunctions/AllBottom.h"

using namespace psr;
using namespace std;

namespace psr {

const AllBottom<BinaryDomain> *ALL_BOTTOM =
    new AllBottom<BinaryDomain>(BinaryDomain::BOTTOM);

} // namespace psr
