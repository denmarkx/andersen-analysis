#include "Andersen.h"

#include <llvm/Support/raw_ostream.h>
#include <graphviz/gvc.h>
#include <graphviz/cgraph.h>

static bool DumpDebugInfo = false;
static bool DumpConstraintInfo = 1;
static bool DumpResultInfo = 1;
static bool SilenceEmptyPtsSetInfo = true;

bool Andersen::runOnModule(const Module &M) {
  nodeFactory.setDataLayout(&M.getDataLayout());
  collectConstraints(M);

  if (DumpDebugInfo)
    dumpConstraintsPlainVanilla();

  optimizeConstraints();

  if (DumpConstraintInfo)
    dumpConstraints();

  solveConstraints();

  if (DumpDebugInfo) {
    errs() << "\n";
    dumpPtsGraphPlainVanilla();
  }

  if (DumpResultInfo) {
    nodeFactory.dumpNodeInfo();
    errs() << "\n";
    dumpPtsGraphPlainVanilla();
  }

  writeGraph();
  return false;
}

void Andersen::printPointsToSet(const llvm::Value *value) {
    PtsSetType ptsSet;
    getPointsToSet(value, ptsSet);

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

void Andersen::writeGraph() const {
  GVC_t *gvc = gvContext();
  Agraph_t *g = agopen("G", Agdirected, nullptr);

  std::unordered_map<unsigned int, Agnode_t*> m;

  for (unsigned i = 0, e = nodeFactory.getNumNodes(); i < e; ++i) {
      NodeIndex rep = nodeFactory.getMergeTarget(i);
      const Value *v = nodeFactory.getValueForNode(rep);
      if (!v) continue;
      std::string str = v->getName().str();
      llvm::raw_string_ostream stream(str);
      v->print(stream);

      std::string x = stream.str();
      Agnode_t *n = agnode(g, const_cast<char*>(x.c_str()), 1);
      m[rep] = n;
  }

  for (auto &[id, node] : m) {
      NodeIndex rep = nodeFactory.getMergeTarget(id);
      auto ptsItr = ptsGraph.find(rep);
      if (ptsItr != ptsGraph.end()) {
        for (auto v : ptsItr->second) {
          Agedge_t *e = agedge(g, node, m[v], "", 1);
        }
      }
  }

  gvLayout(gvc, g, "dot");
  gvRenderFilename(gvc, g, "png", "output.png");
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
