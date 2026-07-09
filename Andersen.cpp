#include "Andersen.h"
#include "NodeFactory.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Module.h"
#include <queue>
#include <unordered_set>

using namespace llvm;

Andersen::Andersen(const Module &module) { runOnModule(module); }

/*
 * Determines if valueA is an alias of valueB. Returns AliasResult:
 *  - NoAlias
 *  - MayAlias
 *  - MustAlias
*/
llvm::AliasResult Andersen::alias(const Value *valueA, const Value *valueB) {
    if (!valueA || !valueB) return AliasResult::NoAlias;
    if (!valueA->getType()->isPointerTy() || !valueB->getType()->isPointerTy()) return AliasResult::NoAlias;
    if (valueA == valueB) return AliasResult::MustAlias;

    MemoryLocation m1(valueA, MemoryLocation::UnknownSize);
    MemoryLocation m2(valueB, MemoryLocation::UnknownSize);

    NodeIndex n1 = nodeFactory.getMergeTarget(nodeFactory.getValueNodeFor(valueA));
    NodeIndex n2 = nodeFactory.getMergeTarget(nodeFactory.getValueNodeFor(valueB));
    
    // Merge target is the same: we'll say it aliases:
    if (n1 == n2)
      return AliasResult::MustAlias;

    auto itr1 = ptsGraph.find(n1);
    auto itr2 = ptsGraph.find(n2);

    // If we know nothing about at least one, we'll say this may alias.
    if (itr1 == ptsGraph.end() || itr2 == ptsGraph.end()) return AliasResult::MayAlias;

    AndersPtsSet &s1 = itr1->second;
    AndersPtsSet &s2 = itr2->second;

    // If any of them is null, we know they do not alias.
    bool isNull1 = s1.isSetContainingOnly(nodeFactory.getNullObjectNode());
    bool isNull2 = s2.isSetContainingOnly(nodeFactory.getNullObjectNode());
    if (isNull1 || isNull2)
      return AliasResult::NoAlias;

    // This is a bit conservative, but it helps prior to checking each node.
    if (s1.getSize() == 1 && s2.getSize() == 1 && *s1.begin() == *s2.begin())
      return AliasResult::MustAlias;

    // If s1 and s2 are the same except for nodes 0-3, we'll say this must alias.
    if (s1.compareExclude(s2))
      return AliasResult::MustAlias;

    // If s1 and s2 contain any overlapping values, except for 0-3, this may alias..
    if (s1.compareIntersectionExclude(s2))
        return AliasResult::MayAlias;

    return AliasResult::NoAlias;
    
}

/*
 * Fills in the transitive pointsTo set for a given context.
 * This differs from getPointsToSet in the fact that the context is not the default context ID.
*/
void Andersen::fillPointsToSet(const llvm::Value* v, PtsSetType &ptsSet) {
    NodeIndex ptrTgt = nodeFactory.getMergeTarget(
        nodeFactory.getValueNodeFor(v));
    if (ptrTgt == AndersNodeFactory::InvalidIndex) return;

    std::unordered_set<NodeIndex> visited;
    std::queue<NodeIndex> worklist;

    auto ptsItr = ptsGraph.find(ptrTgt);
    if (ptsItr == ptsGraph.end()) return;
    
    for (auto vx : ptsItr->second) {
        if (vx == nodeFactory.getNullObjectNode()) continue;
        if (visited.insert(vx).second)
            worklist.push(vx);
    }

    while (!worklist.empty()) {
        unsigned int c = worklist.front();
        worklist.pop();

        const llvm::Value *cv = nodeFactory.getValueForNode(c);
        if (!cv) {
            NodeIndex cur = c;
            while (cur != AndersNodeFactory::InvalidIndex && !cv) {
                NodeIndex base = nodeFactory.getFieldBaseObject(cur);
                if (base == AndersNodeFactory::InvalidIndex) break;
                cv = nodeFactory.getValueForNode(base);
                cur = base;
            }
        }
        if (!cv) continue;

        if (std::find(ptsSet.begin(), ptsSet.end(), cv) == ptsSet.end())
            ptsSet.push_back(cv);

        auto ptsItr2 = ptsGraph.find(c);
        if (ptsItr2 == ptsGraph.end()) continue;
        for (auto vx : ptsItr2->second) {
            if (vx == nodeFactory.getNullObjectNode()) continue;
            if (visited.insert(vx).second)
                worklist.push(vx);
        }
    }
}

/*
 * Places all the reachable values from the given value into the ptsSet.
*/
void Andersen::getPointsToSet(const llvm::Value *v, PtsSetType &ptsSet) {
    fillPointsToSet(v, ptsSet);
}
