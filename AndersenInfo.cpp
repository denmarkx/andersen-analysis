#include "Andersen.h"

#include <llvm/Support/raw_ostream.h>

static bool DumpDebugInfo = false;
static bool DumpConstraintInfo = 1;
static bool DumpResultInfo = 1;
static bool SilenceEmptyPtsSetInfo = true;

void Andersen::runConstraintSolver() {
  if (DumpDebugInfo)
    dumpConstraintsPlainVanilla();

  optimizeConstraints();

  if (DumpConstraintInfo)
    dumpConstraints();

  solveConstraints();
}

void Andersen::dump() const {
  if (DumpDebugInfo) {
    errs() << "\n";
    dumpPtsGraphPlainVanilla();
  }

  if (DumpResultInfo) {
    nodeFactory.dumpNodeInfo();
    errs() << "\n";
    dumpPtsGraphPlainVanilla();
  }
}

bool Andersen::runOnModule(const Module &M, bool solveCnstrs) {
  collectConstraints(M);

  if (solveCnstrs)
    runConstraintSolver();

  dump();
  return false;
}

void Andersen::printPointsToSet(const llvm::Value *value, const SmallVector<const llvm::Value*, 4> contextObjects) {
    PtsSetType ptsSet;
    ContextType context = NoContext;

    for (const auto &v : contextObjects) {
      NodeIndex objIdx = nodeFactory.getObjectNodeFor(v);
      if (objIdx == AndersNodeFactory::InvalidIndex)
          objIdx = nodeFactory.getValueNodeFor(v);
      assert(objIdx != AndersNodeFactory::InvalidIndex);
      context.push_back(objIdx);
    }

    getPointsToSet(value, ptsSet, context);

    if (SilenceEmptyPtsSetInfo && ptsSet.empty()) return;

    errs() << "========================= Points To Set ==============================\n";

    errs() << "    Value: ";
    if (isa<Function>(value))
        errs() << " [F] " << value->getName() << "\n";
    else
        errs() << *value << "\n";

    errs() << "    Set:\n";
    if (ptsSet.empty()) {
        errs() << "        (empty)\n\n";
        return;
    }

    for (const llvm::Value *v : ptsSet) {
        if (isa<Function>(v))
            errs() << "        [F] " << v->getName() << "\n";
        else
            errs() << "        " << *v << "\n";
    }
    errs() << "\n";
}

void Andersen::dumpConstraint(const AndersConstraint &item) const {
  NodeIndex dest = item.getDest();
  NodeIndex src = item.getSrc();

  switch (item.getType()) {
  case AndersConstraint::COPY: {
    nodeFactory.dumpNode(dest);
    errs() << " = ";
    nodeFactory.dumpNode(src);
    break;
  }
  case AndersConstraint::LOAD: {
    nodeFactory.dumpNode(dest);
    errs() << " = *";
    nodeFactory.dumpNode(src);
    break;
  }
  case AndersConstraint::STORE: {
    errs() << "*";
    nodeFactory.dumpNode(dest);
    errs() << " = ";
    nodeFactory.dumpNode(src);
    break;
  }
  case AndersConstraint::ADDR_OF: {
    nodeFactory.dumpNode(dest);
    errs() << " = &";
    nodeFactory.dumpNode(src);
    break;
  }
  case AndersConstraint::GEP: {
    nodeFactory.dumpNode(dest);
    errs() << " = &";
    nodeFactory.dumpNode(src);
    break;
  }
  }

  FieldType fields = item.getFields();
  if (!fields.empty()) {
    errs() << " | Fields = [";
    for (const auto &x : fields) {
      errs() << x << " ";
    }
    errs() << "]";
  }

  errs() << "\n";
}

void Andersen::dumpConstraints() const {
  errs() << "\n----- Constraints -----\n";
  for (auto const &item : constraints)
    dumpConstraint(item);
  errs() << "----- End of Print -----\n";
}

void Andersen::dumpConstraintsPlainVanilla() const {
  for (auto const &item : constraints) {
    errs() << item.getType() << " " << item.getDest() << " " << item.getSrc()
           << " 0\n";
  }
}

void Andersen::dumpPtsGraphPlainVanilla() const {
  for (unsigned i = 0, e = nodeFactory.getNumNodes(); i < e; ++i) {
    NodeIndex rep = nodeFactory.getMergeTarget(i);
    auto ptsItr = ptsGraph.find(rep);
    if (ptsItr != ptsGraph.end()) {
      errs() << i << " ";
      for (auto v : ptsItr->second)
        errs() << v << " ";
      errs() << "\n";
    }
  }
}

void AndersenAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
}

bool AndersenAAWrapperPass::runOnModule(Module &m) {
  result.reset(new Andersen(m));
  return false;
}

AndersenAAWrapperPass::AndersenAAWrapperPass() : ModulePass(ID) {}

char AndersenAAWrapperPass::ID = 0;
static RegisterPass<AndersenAAWrapperPass>
    X("anders-aa", "Andersen Alias Analysis", true, true);
