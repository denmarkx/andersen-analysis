#include "NodeMap.h"
#include "llvm/IR/Instructions.h"

/*
 * Given a parameters, attempts to find an instruction within its users that
 * provides information regarding the pointer type and if it's an aggregate.
 * More specifically, it attempts to identify a GEP instruction.
*/
const llvm::Value* NodeMap::findAggregateFromParam(const llvm::Value *param) const {
    if (!param->getType()->isPointerTy()) return nullptr;

    // If this is a gep already..or allocas, we return it.
    if (isAggregateGEP(param) || isa<AllocaInst>(param))
        return param;

    // Load instructions are particularly useful:
    if (isa<LoadInst>(param)) {
        // This is only hit when previously itr = users().end.
        // ..so the user, in this case, is the 1st operand.
        return findAggregateFromParam(dyn_cast<LoadInst>(param)->getOperand(0));
    }

    auto itr = std::find_if(param->users().begin(), param->users().end(), [&](const User *user) {
        // I don't think it's important to handle every such case,
        // but if it's a GEP instruction, then that's a smoking gun.
        return
            (
                isAggregateGEP(user) ||
                isa<LoadInst>(user)
            );
    });

    if (itr == param->users().end()) {
        // Let's move up to the original callsite and check the arg's uses.
        // I suppose only if we're still a formal argument:
        const Argument *formal = dyn_cast<Argument>(param);
        if (!formal) return nullptr;

        if (1) {
            // const Function *function = formal->getParent();

            // // Get the index of this param.
            // unsigned int paramId = ~0u;
            // for (unsigned int i=0; i < function->arg_size(); i++)
            //     if (function->getArg(i) == formal)
            //         paramId = i;

            // if (paramId == ~0u)
            //     return nullptr;

            // const CallBase *call = dyn_cast<CallBase>(ctx->callSite);

            // // Then get the argument from the call:
            // return findAggregateFromParam(call->getArgOperand(paramId));
        }
        return nullptr;
    }

    return *itr;
}
