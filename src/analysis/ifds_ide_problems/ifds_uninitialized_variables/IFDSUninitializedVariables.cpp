#include "IFDSUninitializedVariables.hh"

IFDSUnitializedVariables::IFDSUnitializedVariables(LLVMBasedICFG &icfg)
    : DefaultIFDSTabulationProblem(icfg) {
  DefaultIFDSTabulationProblem::zerovalue = createZeroValue();
}

shared_ptr<FlowFunction<const llvm::Value *>>
IFDSUnitializedVariables::getNormalFlowFunction(const llvm::Instruction *curr,
                                                const llvm::Instruction *succ) {
  cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% getNormalFlowFunction()"
       << endl;
  // set every local variable as uninitialized, that is not a function parameter
  if (icfg.getMethodName(curr) == "main" && icfg.isStartPoint(curr)) {
  	const llvm::Function* func = icfg.getMethodOf(curr);

  	// set all locals as uninitialized flow function
  	struct UVFF : FlowFunction<const llvm::Value *> {
      const llvm::Function *func;
      const llvm::Value *zerovalue;
      UVFF(const llvm::Function *f, const llvm::Value *zv)
          : func(f), zerovalue(zv) {}
      set<const llvm::Value *> computeTargets(const llvm::Value *source) {
        if (source == zerovalue) {
          set<const llvm::Value *> res;
          // first add all local values of primitive types
          for (auto& BB : *func) {
          	for (auto& inst : BB) {
          		if (const llvm::AllocaInst* alloc = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
          			auto alloc_type = alloc->getAllocatedType();
          			if (alloc_type->isIntegerTy() ||
          					alloc_type->isFloatingPointTy() ||
										alloc_type->isPointerTy() ||
										alloc_type->isArrayTy()) {
          				res.insert(alloc);
          			}
          		} else {
          			// when the very first instruction immediately uses an undef value
          			for (auto& operand : inst.operands()) {
          				if (const llvm::UndefValue* undef = llvm::dyn_cast<llvm::UndefValue>(&operand)) {
          				  res.insert(&inst);
          				}
          			}
          		}
          	}
          }
          // now remove those values that are obtained by function parameters of
          // the entry function
          for (auto &arg : func->getArgumentList()) {
            for (auto user : arg.users()) {
              if (const llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(user)) {
                res.erase(store->getPointerOperand());
              }
            }
          }
          res.insert(zerovalue);
          return res;
        }
        return set<const llvm::Value *>{};
      }
    };
    return make_shared<UVFF>(func, zerovalue);
  }

  // check the all store instructions
  if (const llvm::StoreInst* store = llvm::dyn_cast<llvm::StoreInst>(curr)) {
    const llvm::Value *valueop = store->getValueOperand();
    const llvm::Value *pointerop = store->getPointerOperand();

    struct UVFF : FlowFunction<const llvm::Value *> {
      const llvm::Value *valueop;
      const llvm::Value *pointerop;
      UVFF(const llvm::Value *vop, const llvm::Value *pop)
          : valueop(vop), pointerop(pop) {}
      set<const llvm::Value *> computeTargets(const llvm::Value *source) {
        // check if an uninitialized value is loaded and stored in a variable,
        // then the variable is uninitialized!
        for (auto &use : valueop->uses()) {
          if (const llvm::LoadInst* load = llvm::dyn_cast<llvm::LoadInst>(use)) {
            // if the following is uninit, then this store must be uninit as well!
            if (load->getPointerOperand() == source) {
              return { source, pointerop };
            }
          }
        }
        // otherwise the value is initialized through this store and thus can be
        // killed
        if (pointerop == source) {
          return {};
        } else {
          return { source };
        }
      }
    };
    return make_shared<UVFF>(valueop, pointerop);
  }

  // check if some instruction is using an undefined value directly
  for (auto& operand : curr->operands()) {
  	if (const llvm::UndefValue* undef = llvm::dyn_cast<llvm::UndefValue>(operand)) {
  		return make_shared<Gen<const llvm::Value *>>(curr, zeroValue());
  	}
  }

  // otherwise we do not care and nothing changes
  return Identity<const llvm::Value *>::v();
}

shared_ptr<FlowFunction<const llvm::Value *>>
IFDSUnitializedVariables::getCallFlowFuntion(const llvm::Instruction *callStmt,
                                             const llvm::Function *destMthd) {
  cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% getCallFlowFunction()"
       << endl;
  // check for a usual function call
  if (const llvm::CallInst* call = llvm::dyn_cast<llvm::CallInst>(callStmt)) {
    if (call->getCalledFunction()) {
    	cout << "DIRECT CALL TO: " << destMthd->getName().str() << endl;
    } else {
    	cout << "INDIRECT CALL TO: " << destMthd->getName().str() << endl;
    }

  	// collect the actual parameters
    vector<const llvm::Value *> actuals;
    for (auto& operand : call->operands()) {
      	actuals.push_back(operand);
    }

    cout << "ACTUALS:" << endl;
    for (auto a : actuals) {
      if (a)
        a->dump();
    }

    struct UVFF : FlowFunction<const llvm::Value *> {
      const llvm::Function *destMthd;
      const llvm::CallInst *call;
      vector<const llvm::Value *> actuals;
      const llvm::Value *zerovalue;
      UVFF(const llvm::Function *dm, const llvm::CallInst* c, vector<const llvm::Value *> atl,
           const llvm::Value *zv)
          : destMthd(dm), call(c), actuals(atl), zerovalue(zv) {}
      set<const llvm::Value *> computeTargets(const llvm::Value *source) override {
      	// do the mapping from actual to formal parameters
      	for (size_t i = 0; i < actuals.size(); ++i) {
      		if (actuals[i] == source) {
      			cout << "ACTUAL == SOURCE" << endl;
      			return { call->getOperand(i) };
      		}
//      		if (const llvm::UndefValue* undef = llvm::dyn_cast<llvm::UndefValue>(actuals[i])) {
//      			return { undef };
//      		}
      	}

        if (source == zerovalue) {
          // gen all locals that are not parameter locals!!!
          // make a set of all uninitialized local variables!
          set<const llvm::Value *> uninitlocals;
          for (auto& BB : *destMthd) {
          	for (auto& inst : BB) {
          		if (const llvm::AllocaInst* alloc = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
          			// check if the allocated value is of a primitive type
          			auto alloc_type = alloc->getAllocatedType();
          			if (alloc_type->isIntegerTy() ||
          					alloc_type->isFloatingPointTy() ||
										alloc_type->isPointerTy() ||
										alloc_type->isArrayTy()) {
          				uninitlocals.insert(alloc);
          			}
          		} else {
          			for (auto& operand : inst.operands()) {
          				if (const llvm::UndefValue* undef = llvm::dyn_cast<llvm::UndefValue>(&operand)) {
          					uninitlocals.insert(operand);
          				}
          			}
          		}
          	}
          }
          // remove all local variables, that are initialized formal parameters!
          for (auto &arg : destMthd->getArgumentList()) {
            uninitlocals.erase(&arg);
          }
          return uninitlocals;
        }
        return set<const llvm::Value *>{};
      }
    };
    return make_shared<UVFF>(destMthd, call, actuals, zerovalue);
  } else if (const llvm::InvokeInst* invoke = llvm::dyn_cast<llvm::InvokeInst>(callStmt)) {
    /*
     * TODO consider an invoke statement
     * An invoke statement must be treated the same as an ordinary call
     * statement
     */
    return Identity<const llvm::Value *>::v();
  }
  cout << "error when getCallFlowFunction() was called\n"
  				"instruction is neither a call- nor an invoke instruction!"
  		<< endl;
  DIE_HARD;
  return nullptr;
}

shared_ptr<FlowFunction<const llvm::Value *>>
IFDSUnitializedVariables::getRetFlowFunction(const llvm::Instruction *callSite,
                                             const llvm::Function *calleeMthd,
                                             const llvm::Instruction *exitStmt,
                                             const llvm::Instruction *retSite) {
  cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% getRetFlowFunction()"
       << endl;
  // consider it a value gets store at the call site:
  // int x = call(...);
  // x shall be uninitialized then
  // check if callSite is usual call instruction
  if (const llvm::ReturnInst* ret = llvm::dyn_cast<llvm::ReturnInst>(exitStmt)) {

  	struct UVFF : FlowFunction<const llvm::Value*> {
  		const llvm::CallInst* call;
  		const llvm::ReturnInst* ret;
  		UVFF(const llvm::CallInst* c, const llvm::ReturnInst* r) : call(c), ret(r) {}
  		set<const llvm::Value*> computeTargets(const llvm::Value* source) {
  			if (ret->getNumOperands() > 0 && ret->getOperand(0) == source) {
  				set<const llvm::Value*> results;
  				// users of this call instruction get an uninitialized value!
  				for (auto user : call->users()) {
  					results.insert(user);
  				}
  				if (results.empty()) {
  					results.insert(call);
  				}
  				return results;
  			}
  			return {};
  		}
  	};
  	if (const llvm::CallInst* call = llvm::dyn_cast<llvm::CallInst>(callSite)) {
  		return make_shared<UVFF>(call, ret);
  	}
  }
  return KillAll<const llvm::Value*>::v();
}

shared_ptr<FlowFunction<const llvm::Value *>>
IFDSUnitializedVariables::getCallToRetFlowFunction(
    const llvm::Instruction *callSite, const llvm::Instruction *retSite) {
  cout << "%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% getCallToRetFlowFunction()"
       << endl;
  // handle a normal use of an initialized return value
  for (auto user : callSite->users()) {
  	return make_shared<Kill<const llvm::Value *>>(user);
  }
  return Identity<const llvm::Value *>::v();
}

shared_ptr<FlowFunction<const llvm::Value *>>
IFDSUnitializedVariables::getSummaryFlowFunction(const llvm::Instruction *callStmt,
											 	 	 	 	 	 	 	 	 	 	 	 	 	 const llvm::Function *destMthd) {
		return Identity<const llvm::Value*>::v();
}

map<const llvm::Instruction *, set<const llvm::Value *>>
IFDSUnitializedVariables::initialSeeds() {
  const llvm::Function *mainfunction = icfg.getModule().getFunction("main");
  const llvm::Instruction *firstinst = &(*mainfunction->begin()->begin());
  set<const llvm::Value *> iset{zeroValue()};
  map<const llvm::Instruction *, set<const llvm::Value *>> imap{
      {firstinst, iset}};
  return imap;
}

const llvm::Value *IFDSUnitializedVariables::createZeroValue() {
  // create a special value to represent the zero value!
  static ZeroValue *zero = new ZeroValue;
	return zero;
}