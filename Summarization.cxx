#include "Summarization.h"

#include "llvm/ADT/TypeSwitch.h"
#include "llvm/IR/Instructions.h"
#include <iostream>
#include <queue>

void Summarization::summarizeParameters(const Function *f, unsigned int fObjIdx) {
    SmallVector<unsigned, 4> indices;
    SmallVector<ParameterSummary, 4> summaries;

    int idx = 0;
    for (const Argument &p : f->args()) {
        if (auto option = summarizeParameter(p)) {
            summaries.push_back(*option);
            indices.push_back(idx);
        }
        idx++;
    }

    // Len of summaries is just a litmus test to see if this is worth tracking:
    if (!summaries.empty())
        _summaries[fObjIdx] = ParameterSummaryGroup { indices, summaries };
}

const SmallVector<unsigned, 4> Summarization::getParameterIndices(unsigned int fObjIdx) {
    if (!_summaries.contains(fObjIdx)) return {};
    return _summaries.find(fObjIdx)->getSecond().parameterIndices;
}

std::optional<ParameterSummary> Summarization::summarizeParameter(const Argument &arg) {
    // Ignoring if not a ptr type.
    if (!arg.getType()->isPointerTy()) return {};

    ParameterSummary summary;
    std::queue<const Value*> q;
    
    const auto queueUsers = [&q](const Value *v) {
        for (const User *user : v->users())
            q.push(user);
    };

    // For a parameter to be considered for contextualization, it must satisfy at least 1
    // boolean from ParameterSummary...meaning we are done once we find that one.
    queueUsers(&arg);
    while (!q.empty()) {
        const Value *front = q.front();
        q.pop();

        const Instruction *cur = dyn_cast<Instruction>(front);
        if (!cur) continue;

        TypeSwitch<const Value*>(cur)
            .Case<llvm::LoadInst>([&](const llvm::LoadInst *inst) {
                if (inst->getPointerOperandType())
                    summary.loadsPointer = true;
            })

            .Case<llvm::StoreInst>([&](const llvm::StoreInst *inst) {
                if (inst->getPointerOperandType())
                    summary.storesPointer = true;
            })

            .Case<llvm::CallBase>([&](const llvm::CallBase *inst) {
                // TODO: need to check if this the param is an arg in the call and is not an indirect
                summary.escapes = true;
            })

            .Default([](const llvm::Value *v) {});

        // break entirely if satisfied.
        if (summary.requiresContext()) {
            std::cout << summary << "\n";
            return summary;
        }

        queueUsers(cur);
    }

    return {};
}

std::ostream& operator<<(std::ostream& out, const ParameterSummary &summary) {
    out << "ParameterSummary:\n" << 
        "   LP: " << summary.loadsPointer << "\n" <<
        "   SP: " << summary.storesPointer << "\n" <<
        "   ES: " << summary.escapes << "\n";
    return out;
}
