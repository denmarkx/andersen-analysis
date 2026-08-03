#define DEBUG_TYPE "andersen"

#include "Andersen.h"
#include "NodeFactory.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/Module.h"

#include <unordered_set>
#include <queue>

using namespace llvm;

Andersen::Andersen(const Module &module) { runOnModule(module); }

/*
 * Determines if valueA is an alias of valueB for ALL of the given contexts:
 *   For any context, 
 *      if there is a mix of NoAlias and (MayAlias or MustAlias), we assume MayAlias.
 *      if there is only NoAlias, we ret NoAlias
 *      if there is only MustAlias, we ret MustAlias
*/
llvm::AliasResult Andersen::alias(const Value *valueA, const Value *valueB) {
    int noAlias = 0;
    int mayAlias = 0;
    int mustAlias = 0;

    const auto add = [&](AliasResult result) {
        switch(result) {
            case llvm::AliasResult::NoAlias: { noAlias++; break; }
            case llvm::AliasResult::MayAlias: { mayAlias++; break; }
            case llvm::AliasResult::MustAlias: { mustAlias++; break; }
            default: break;
        }
    };

    NodeIndex valueAIdx = nodeFactory.getValueNodeFor(valueA);
    assert(valueAIdx != AndersNodeFactory::InvalidIndex);
    
    NodeIndex valueBIdx = nodeFactory.getValueNodeFor(valueB);
    assert(valueBIdx != AndersNodeFactory::InvalidIndex);

    for (const auto &context : nodeFactory.getAllContexts(valueAIdx))
        add(alias(valueA, context, valueB, context));

    for (const auto &context : nodeFactory.getAllContexts(valueBIdx))
        add(alias(valueA, context, valueB, context));

    if (noAlias + mayAlias + mustAlias == 0)
        return AliasResult::NoAlias;
    if (noAlias && !mayAlias && !mustAlias)
        return AliasResult::NoAlias;
    if (mustAlias && !mayAlias && !noAlias)
        return AliasResult::MustAlias;
    if (noAlias && (mayAlias || mustAlias))
        return AliasResult::MayAlias;
}

/*
 * Determines if valueA is an alias of valueB. Returns AliasResult:
 *  - NoAlias
 *  - MayAlias
 *  - MustAlias
*/
llvm::AliasResult Andersen::alias(const Value *valueA, const ContextType valueAContext, const Value *valueB, const ContextType valueBContext) {
    if (!valueA || !valueB) return AliasResult::NoAlias;
    if (!valueA->getType()->isPointerTy() || !valueB->getType()->isPointerTy()) return AliasResult::NoAlias;
    if (valueA == valueB) return AliasResult::MustAlias;

    MemoryLocation m1(valueA, MemoryLocation::UnknownSize);
    MemoryLocation m2(valueB, MemoryLocation::UnknownSize);

    NodeIndex valueAIdx = nodeFactory.getValueNodeFor(valueA);
    NodeIndex valueBIdx = nodeFactory.getValueNodeFor(valueB);

    if (valueAIdx == AndersNodeFactory::InvalidIndex || valueBIdx == AndersNodeFactory::InvalidIndex) {
        LLVM_DEBUG(dbgs() << "(alias): valueA and/or valueB is invalid.");
        return AliasResult::NoAlias;
    }

    NodeIndex n1 = nodeFactory.getMergeTarget(valueAIdx);
    NodeIndex n2 = nodeFactory.getMergeTarget(valueBIdx);
    
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
void Andersen::fillPointsToSet(const llvm::Value* v, PtsSetType &ptsSet, const ContextType context) {
    if (!v->getType()->isPointerTy()) return;
    
    NodeIndex vIdx = nodeFactory.getValueNodeFor(v, context);
    assert(vIdx != AndersNodeFactory::InvalidIndex);

    NodeIndex ptrTgt = nodeFactory.getMergeTarget(vIdx);
    assert(ptrTgt != AndersNodeFactory::InvalidIndex);

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
            assert(cur != AndersNodeFactory::InvalidIndex);
            while (cur != AndersNodeFactory::InvalidIndex && !cv) {
                NodeIndex base = nodeFactory.getFieldBaseObject(cur);
                assert(base != AndersNodeFactory::InvalidIndex);
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
void Andersen::getPointsToSet(const llvm::Value *v, PtsSetType &ptsSet, const ContextType context) {
    if (v) fillPointsToSet(v, ptsSet, context);
}

/*
 * Places all the reachable values from the given value into the ptsSet.
*/
void Andersen::getPointsToSet(const llvm::Value *v, PtsSetType &ptsSet, const SmallVector<const llvm::Value*, 4> contextObjects) {
    if (!v) return;

    ContextType context = NoContext;
    for (const auto &o : contextObjects) {
        NodeIndex objIdx = nodeFactory.getObjectNodeFor(o);
        if (objIdx == AndersNodeFactory::InvalidIndex)
            objIdx = nodeFactory.getValueNodeFor(o);
        assert(objIdx != AndersNodeFactory::InvalidIndex);
        context.push_back(objIdx);
    }

    fillPointsToSet(v, ptsSet, context);
}
