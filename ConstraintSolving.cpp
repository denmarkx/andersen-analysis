#include "Andersen.h"
#include "CycleDetector.h"
#include "NodeFactory.h"
#include "PtsSet.h"
#include "SparseBitVectorGraph.h"
#include "Constraint.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <iostream>
#include <map>
#include <queue>

using namespace llvm;

bool EnableHCD = true;
bool EnableLCD = true;
// cl::opt<bool>
//     EnableHCD("enable-hcd",
//               cl::desc("Enable the hybrid cycle detection algorithm"));
// cl::opt<bool> EnableLCD("enable-lcd",
//                         cl::desc("Enable the lazy cycle detection algorithm"));

namespace {

struct GepEdge {
    NodeIndex dst;
    FieldType fields;
};

// This class represent the constraint graph
class ConstraintGraphNode {
private:
  NodeIndex idx;
  SmallVector<GepEdge, 4> GepEdges;

  using NodeSet = llvm::SmallVector<NodeIndex, 4>;

  NodeSet copyEdges;
  NodeSet loadEdges;
  NodeSet storeEdges;

  static bool insertEdge(NodeSet &vec, NodeIndex dst) {
    if (llvm::is_contained(vec, dst))
      return false;
    vec.push_back(dst);
    return true;
  }

  static bool removeEdge(NodeSet &vec, NodeIndex dst) {
    auto it = llvm::find(vec, dst);
    if (it == vec.end())
      return false;

    *it = vec.back();
    vec.pop_back();
    return true;
  }

  bool insertCopyEdge(NodeIndex dst) { return insertEdge(copyEdges, dst); }
  bool removeCopyEdge(NodeIndex dst) { return removeEdge(copyEdges, dst); }

  bool insertLoadEdge(NodeIndex dst) { return insertEdge(loadEdges, dst); }
  bool removeLoadEdge(NodeIndex dst) { return removeEdge(loadEdges, dst); }

  bool insertStoreEdge(NodeIndex dst) { return insertEdge(storeEdges, dst); }
  bool removeStoreEdge(NodeIndex dst) { return removeEdge(storeEdges, dst); }

  bool addGepEdge(NodeIndex dst, FieldType fields) {
    GepEdges.push_back({dst, fields});
    return true;
  }

  bool isEmpty() const {
    return copyEdges.empty() && loadEdges.empty() && storeEdges.empty();
  }

  void mergeEdges(const ConstraintGraphNode &other) {
    for (auto e : other.copyEdges)
      insertCopyEdge(e);

    for (auto e : other.loadEdges)
      insertLoadEdge(e);

    for (auto e : other.storeEdges)
      insertStoreEdge(e);
  }

  ConstraintGraphNode(NodeIndex i) : idx(i) {
    copyEdges.reserve(4);
    loadEdges.reserve(2);
    storeEdges.reserve(2);
  }

public:
  using iterator = NodeSet::iterator;
  using const_iterator = NodeSet::const_iterator;

  NodeIndex getNodeIndex() const { return idx; }

  bool replaceCopyEdge(NodeIndex oldIdx, NodeIndex newIdx) {
    return removeCopyEdge(oldIdx) && insertCopyEdge(newIdx);
  }

  bool replaceLoadEdge(NodeIndex oldIdx, NodeIndex newIdx) {
    return removeLoadEdge(oldIdx) && insertLoadEdge(newIdx);
  }

  bool replaceStoreEdge(NodeIndex oldIdx, NodeIndex newIdx) {
    return removeStoreEdge(oldIdx) && insertStoreEdge(newIdx);
  }

  void replaceGepEdge(NodeIndex oldDst, NodeIndex newDst) {
    for (auto &e : GepEdges) {
      if (e.dst == oldDst)
        e.dst = newDst;
    }
  }

  iterator begin() { return copyEdges.begin(); }
  iterator end() { return copyEdges.end(); }

  const_iterator begin() const { return copyEdges.begin(); }
  const_iterator end() const { return copyEdges.end(); }

  const_iterator load_begin() const { return loadEdges.begin(); }
  const_iterator load_end() const { return loadEdges.end(); }

  llvm::iterator_range<const_iterator> loads() const {
    return {load_begin(), load_end()};
  }

  const_iterator store_begin() const { return storeEdges.begin(); }
  const_iterator store_end() const { return storeEdges.end(); }

  llvm::iterator_range<const_iterator> stores() const {
    return {store_begin(), store_end()};
  }

  auto gep_begin() { return GepEdges.begin(); }
  auto gep_end()   { return GepEdges.end(); }

  auto gep_begin() const { return GepEdges.begin(); }
  auto gep_end()   const { return GepEdges.end(); }

  const SmallVector<GepEdge,4>& geps() const {
    return GepEdges;
  }

  friend class ConstraintGraph;
};

class ConstraintGraph {
private:
  typedef llvm::DenseMap<NodeIndex, ConstraintGraphNode> NodeMapTy;
  NodeMapTy graph;

public:
  typedef NodeMapTy::iterator iterator;
  typedef NodeMapTy::const_iterator const_iterator;

  ConstraintGraph() {
    graph.reserve(1 << 17);
  }

  bool insertCopyEdge(NodeIndex src, NodeIndex dst) {
    auto itr = graph.find(src);
    if (itr == graph.end()) {
      ConstraintGraphNode srcNode(src);
      srcNode.insertCopyEdge(dst);
      graph.insert(std::make_pair(src, std::move(srcNode)));
      return true;
    } else
      return (itr->second).insertCopyEdge(dst);
  }

  bool insertLoadEdge(NodeIndex src, NodeIndex dst) {
    auto itr = graph.find(src);
    if (itr == graph.end()) {
      ConstraintGraphNode srcNode(src);
      srcNode.insertLoadEdge(dst);
      graph.insert(std::make_pair(src, std::move(srcNode)));
      return true;
    } else
      return (itr->second).insertLoadEdge(dst);
  }

  bool insertStoreEdge(NodeIndex src, NodeIndex dst) {
    auto itr = graph.find(src);
    if (itr == graph.end()) {
      ConstraintGraphNode srcNode(src);
      srcNode.insertStoreEdge(dst);
      graph.insert(std::make_pair(src, std::move(srcNode)));
      return true;
    } else
      return (itr->second).insertStoreEdge(dst);
  }

  bool insertGepEdge(NodeIndex src,NodeIndex dst,const FieldType &fields) {
    ConstraintGraphNode *node = getOrInsertNode(src);
    return node->addGepEdge(dst, fields);
  }

  void mergeNodes(NodeIndex dst, NodeIndex src) {
    auto itr = graph.find(src);
    if (itr == graph.end())
      return;

    const ConstraintGraphNode &srcNode = itr->second;
    itr = graph.find(dst);
    if (itr == graph.end()) {
      ConstraintGraphNode dstNode(dst);
      graph.insert(std::make_pair(dst, srcNode));
    } else
      (itr->second).mergeEdges(srcNode);
  }

  void deleteNode(NodeIndex idx) { graph.erase(idx); }

  ConstraintGraphNode *getNodeWithIndex(NodeIndex idx) {
    auto itr = graph.find(idx);
    if (itr == graph.end())
      return nullptr;
    else
      return &(itr->second);
  }

  ConstraintGraphNode *getOrInsertNode(NodeIndex idx) {
    auto itr = graph.find(idx);
    if (itr == graph.end()) {
      ConstraintGraphNode newNode(idx);
      itr = graph.insert(std::make_pair(idx, newNode)).first;
    }
    return &(itr->second);
  }

  iterator begin() { return graph.begin(); }
  iterator end() { return graph.end(); }
  const_iterator begin() const { return graph.begin(); }
  const_iterator end() const { return graph.end(); }
};

} // namespace

// Specialize the AnderGraphTraits for ConstraintGraph
template <> class AndersGraphTraits<ConstraintGraph> {
public:
  typedef ConstraintGraphNode NodeType;
  typedef MapValueIterator<ConstraintGraph::const_iterator> NodeIterator;
  typedef ConstraintGraphNode::const_iterator ChildIterator;

  static inline ChildIterator child_begin(const NodeType *n) {
    return n->begin();
  }
  static inline ChildIterator child_end(const NodeType *n) { return n->end(); }

  static inline NodeIterator node_begin(const ConstraintGraph *g) {
    return NodeIterator(g->begin());
  }
  static inline NodeIterator node_end(const ConstraintGraph *g) {
    return NodeIterator(g->end());
  }
};

namespace {

void collapseNodes(NodeIndex dst, NodeIndex src, AndersNodeFactory &nodeFactory,
                   llvm::DenseMap<NodeIndex, AndersPtsSet> &ptsGraph,
                   ConstraintGraph &constraintGraph) {
  if (dst == src)
    return;

  // Node merge
  nodeFactory.mergeNode(dst, src);
  if (ptsGraph.count(src))
    ptsGraph[dst].unionWith(ptsGraph[src]);
  constraintGraph.mergeNodes(dst, src);

  // We don't need the node cycleIdx any more
  ptsGraph.erase(src);
  constraintGraph.deleteNode(src);
}

// The worklist for our analysis
class AndersWorkList {
private:
  // The FIFO queue
  std::queue<NodeIndex> list;
  // Avoid duplicate entries in FIFO queue
  llvm::DenseSet<NodeIndex> set;

public:
  AndersWorkList() {}
  void enqueue(NodeIndex elem) {
    if (!set.count(elem)) {
      list.push(elem);
      set.insert(elem);
    }
  }
  NodeIndex dequeue() {
    assert(!list.empty() && "Trying to dequeue an empty queue!");
    NodeIndex ret = list.front();
    list.pop();
    set.erase(ret);
    return ret;
  }
  bool isEmpty() const { return list.empty(); }
};

// The technique used here is described in "The Ant and the Grasshopper: Fast
// and Accurate Pointer Analysis for Millions of Lines of Code. In Programming
// Language Design and Implementation (PLDI), June 2007." It is known as the
// "HCD" (Hybrid Cycle Detection) algorithm. It is called a hybrid because it
// performs an offline analysis and uses its results during the solving (online)
// phase. This is just the offline portion
class OfflineCycleDetector : public CycleDetector<SparseBitVectorGraph> {
private:
  // The node factory
  AndersNodeFactory &nodeFactory;

  // The offline constraint graph
  SparseBitVectorGraph offlineGraph;
  // If a mapping <p, q> is in this map, it means that *p and q are in the same
  // cycle in the offline constraint graph, and anything that p points to during
  // the online constraint solving phase can be immediately collapse with q
  DenseMap<NodeIndex, NodeIndex> collapseMap;
  // Holds the pairs of VAR nodes that we are going to merge together
  DenseMap<NodeIndex, NodeIndex> mergeMap;
  // Used to collect the scc nodes on a cycle
  SparseBitVector<> scc;

  // Return the node index of the "ref node" (used to represent *n) of n.
  // We won't actually create that ref node. We cannot use the NodeIndex of that
  // refNode to index into nodeFactory
  NodeIndex getRefNodeIndex(NodeIndex n) const {
    return n + nodeFactory.getNumNodes();
  }

  void buildOfflineConstraintGraph(
      const std::vector<AndersConstraint> &constraints) {
    for (auto const &c : constraints) {
      NodeIndex srcTgt = nodeFactory.getMergeTarget(c.getSrc());
      NodeIndex dstTgt = nodeFactory.getMergeTarget(c.getDest());
      switch (c.getType()) {
      case AndersConstraint::ADDR_OF:
        break;
      case AndersConstraint::LOAD: {
        offlineGraph.insertEdge(getRefNodeIndex(srcTgt), dstTgt);
        break;
      }
      case AndersConstraint::STORE: {
        offlineGraph.insertEdge(srcTgt, getRefNodeIndex(dstTgt));
        break;
      }
      case AndersConstraint::COPY: {
        offlineGraph.insertEdge(srcTgt, dstTgt);
        break;
      }
      case AndersConstraint::GEP: {
        offlineGraph.insertEdge(srcTgt, dstTgt);
        break;
      }
      }
    }
  }

  NodeType *getRep(NodeIndex idx) override {
    return offlineGraph.getOrInsertNode(idx);
  }

  // Specify how to process the non-rep nodes if a cycle is found
  void processNodeOnCycle(const NodeType *node,
                          const NodeType *repNode) override {
    scc.set(node->getNodeIndex());
  }

  // Specify how to process the rep nodes if a cycle is found
  void processCycleRepNode(const NodeType *node) override {
    // A trivial cycle is not interesting
    if (scc.count() == 0)
      return;

    scc.set(node->getNodeIndex());

    // The representative is the first non-ref node
    NodeIndex repNode = scc.find_first();
    assert(repNode < nodeFactory.getNumNodes() &&
           "The SCC didn't have a non-Ref node!");
    for (auto itr = ++scc.begin(), ite = scc.end(); itr != ite; ++itr) {
      NodeIndex cycleNode = *itr;
      if (cycleNode > nodeFactory.getNumNodes())
        // For REF nodes, insert it to the collapse map
        collapseMap[cycleNode - nodeFactory.getNumNodes()] = repNode;
      else
        // For VAR nodes, insert it to the merge map
        // We don't merge the nodes immediately to avoid affecting the DFS
        mergeMap[cycleNode] = repNode;
    }

    scc.clear();
  }

public:
  OfflineCycleDetector(const std::vector<AndersConstraint> &cs,
                       AndersNodeFactory &n)
      : nodeFactory(n) {
    // Build the offline constraint graph first before we move on
    buildOfflineConstraintGraph(cs);
  }

  void run() override {
    runOnGraph(&offlineGraph);

    // Merge the nodes in mergeMap
    for (auto const &mapping : mergeMap)
      nodeFactory.mergeNode(mapping.second, mapping.first);

    // We don't need these structures any more. The only thing we keep should be
    // those info that are necessary to answer collapsing target queries
    mergeMap.clear();
    offlineGraph.releaseMemory();
    releaseSCCMemory();
  }

  // Return InvalidIndex if no collapse target found
  NodeIndex getCollapseTarget(NodeIndex n) {
    auto itr = collapseMap.find(n);
    if (itr == collapseMap.end())
      return AndersNodeFactory::InvalidIndex;
    else
      return itr->second;
  }
};

void buildConstraintGraph(ConstraintGraph &cGraph,
                          const std::vector<AndersConstraint> &constraints,
                          AndersNodeFactory &nodeFactory,
                          llvm::DenseMap<NodeIndex, AndersPtsSet> &ptsGraph) {
  for (auto const &c : constraints) {
    NodeIndex srcTgt = nodeFactory.getMergeTarget(c.getSrc());
    NodeIndex dstTgt = nodeFactory.getMergeTarget(c.getDest());
    switch (c.getType()) {
    case AndersConstraint::ADDR_OF: {
      // We don't want to replace src with srcTgt because, after all, the
      // address of a variable is NOT the same as the address of another
      // variable
      ptsGraph[dstTgt].insert(c.getSrc());
      break;
    }
    case AndersConstraint::LOAD: {
      cGraph.insertLoadEdge(srcTgt, dstTgt);
      break;
    }
    case AndersConstraint::STORE: {
      cGraph.insertStoreEdge(dstTgt, srcTgt);
      break;
    }
    case AndersConstraint::COPY: {
      cGraph.insertCopyEdge(srcTgt, dstTgt);
      break;
    }
    case AndersConstraint::GEP: {
      cGraph.insertGepEdge(c.getSrc(), c.getDest(), c.getFields());
      break;
    }
    }
  }
}

class OnlineCycleDetector : public CycleDetector<ConstraintGraph> {
private:
  AndersNodeFactory &nodeFactory;
  ConstraintGraph &constraintGraph;
  llvm::DenseMap<NodeIndex, AndersPtsSet> &ptsGraph;
  const DenseSet<NodeIndex> &candidates;

  NodeType *getRep(NodeIndex idx) override {
    return constraintGraph.getOrInsertNode(nodeFactory.getMergeTarget(idx));
  }
  // Specify how to process the non-rep nodes if a cycle is found
  void processNodeOnCycle(const NodeType *node,
                          const NodeType *repNode) override {
    NodeIndex repIdx = nodeFactory.getMergeTarget(repNode->getNodeIndex());
    NodeIndex cycleIdx = nodeFactory.getMergeTarget(node->getNodeIndex());
    // errs() << "Collapse node " << cycleIdx << " with node " << repIdx <<
    // "\n";

    collapseNodes(repIdx, cycleIdx, nodeFactory, ptsGraph, constraintGraph);
  }
  // Specify how to process the rep nodes if a cycle is found
  void processCycleRepNode(const NodeType *node) override {
    // Do nothing, I guess?
  }

public:
  OnlineCycleDetector(AndersNodeFactory &n, ConstraintGraph &co,
                      llvm::DenseMap<NodeIndex, AndersPtsSet> &p,
                      const DenseSet<NodeIndex> &ca)
      : nodeFactory(n), constraintGraph(co), ptsGraph(p), candidates(ca) {}

  void run() override {
    // Perform cycle detection on for nodes on the candidate list
    for (auto node : candidates)
      runOnNode(node);
  }
};

} // end of anonymous namespace

static inline uint64_t encodeEdge(NodeIndex a, NodeIndex b) {
    return (uint64_t(a) << 32) | b;
}

static inline NodeIndex decodeSrc(uint64_t e) {
  return NodeIndex(e >> 32);
}

static inline NodeIndex decodeDst(uint64_t e) {
  return NodeIndex(e & 0xffffffff);
}

static void pruneEdges(
  DenseSet<uint64_t> &edges,
  AndersNodeFactory &factory) {

  DenseSet<uint64_t> valid;
  valid.reserve(edges.size());

  for (uint64_t e : edges) {
    NodeIndex src = decodeSrc(e);
    NodeIndex dst = decodeDst(e);

    if (factory.getMergeTarget(src) == src &&
        factory.getMergeTarget(dst) == dst) {
      valid.insert(e);
    }
  }

  edges = std::move(valid);
}

/// solveConstraints - This stage iteratively processes the constraints list
/// propagating constraints (adding edges to the Nodes in the points-to graph)
/// until a fixed point is reached.
///
/// We use a variant of the technique called "Lazy Cycle Detection", which is
/// described in "The Ant and the Grasshopper: Fast and Accurate Pointer
/// Analysis for Millions of Lines of Code. In Programming Language Design and
/// Implementation (PLDI), June 2007."
/// The paper describes performing cycle detection one node at a time, which can
/// be expensive if there are no cycles, but there are long chains of nodes that
/// it heuristically believes are cycles (because it will DFS from each node
/// without state from previous nodes).
/// Instead, we use the heuristic to build a worklist of nodes to check, then
/// cycle detect them all at the same time to do this more cheaply.  This
/// catches cycles slightly later than the original technique did, but does it
/// make significantly cheaper.
void Andersen::solveConstraints() {
  // We'll do offline HCD first
  OfflineCycleDetector offlineInfo(constraints, nodeFactory);
  if (EnableHCD)
    offlineInfo.run();

  // Now build the constraint graph
  ConstraintGraph constraintGraph;
  buildConstraintGraph(constraintGraph, constraints, nodeFactory, ptsGraph);
  // The constraint vector is useless now
  // constraints.clear();

  // We switch between two work lists instead of relying on only one work list
  AndersWorkList workList1, workList2;
  // The "current" and the "next" work list
  AndersWorkList *currWorkList = &workList1, *nextWorkList = &workList2;
  // The set of nodes that LCD believes might be on a cycle
  DenseSet<NodeIndex> cycleCandidates;
  // The set of edges that LCD believes not on a cycle
  DenseSet<uint64_t> checkedEdges;

  DenseMap<NodeIndex, NodeIndex> updateMap;
  DenseSet<NodeIndex> reseeded;

  ptsGraph.reserve(nodeFactory.getNumNodes());

  DenseMap<NodeIndex, AndersPtsSet> deltaPts;
  deltaPts.reserve(nodeFactory.getNumNodes() * 2);
  for (auto const &mapping : ptsGraph) {
    deltaPts[mapping.first] = mapping.second;
  }

  constexpr unsigned LCD_THRESHOLD = 128;

  auto s = std::chrono::steady_clock::now();
  // Scan the node list, add it to work list if the node a representative and
  // can contribute to the calculation right now.
  for (auto const &mapping : ptsGraph) {
    NodeIndex node = mapping.first;
    if (nodeFactory.getMergeTarget(node) == node &&
        constraintGraph.getNodeWithIndex(node) != nullptr)
      currWorkList->enqueue(node);
  }

  while (!currWorkList->isEmpty()) {
    // Iteration begins

    // First we've got to check if there is any cycle candidates in the last
    // iteration. If there is, detect and collapse cycle
    if (EnableLCD && cycleCandidates.size() >= LCD_THRESHOLD) {
      // Detect and collapse cycles online
      OnlineCycleDetector cycleDetector(nodeFactory, constraintGraph, ptsGraph,
                                        cycleCandidates);
      cycleDetector.run();
      cycleCandidates.clear();
      pruneEdges(checkedEdges, nodeFactory);
    }

    while (!currWorkList->isEmpty()) {
      NodeIndex node = currWorkList->dequeue();
      node = nodeFactory.getMergeTarget(node);
      // errs() << "Examining node " << node << "\n";

      ConstraintGraphNode *cNode = constraintGraph.getNodeWithIndex(node);
      if (cNode == nullptr)
        continue;

      auto deltaItr = deltaPts.find(node);
      if (deltaItr == deltaPts.end() || deltaItr->second.isEmpty())
        continue;
      AndersPtsSet &delta = deltaItr->second;

      auto ptsItr = ptsGraph.find(node);
      if (ptsItr != ptsGraph.end()) {
        // Check indirect constraints and add copy edge to the constraint graph
        // if necessary
        const AndersPtsSet &ptsSet = ptsItr->second;

        // This is where we perform HCD: check if node has a collapse target,
        // and if it does, merge them immediately
        if (EnableHCD) {
          NodeIndex collapseTarget = offlineInfo.getCollapseTarget(node);
          if (collapseTarget != AndersNodeFactory::InvalidIndex) {
            // errs() << "node = " << node << ", collapseTgt = " <<
            // collapseTarget << "\n";
            NodeIndex ctRep = nodeFactory.getMergeTarget(collapseTarget);
            // Here we have to pay special attention to whether the node
            // points-to itself.
            bool mergeSelf = false;
            for (auto v : ptsSet) {
              NodeIndex vRep = nodeFactory.getMergeTarget(v);
              if (vRep == node) {
                mergeSelf = true;
                continue;
              }
              collapseNodes(ctRep, vRep, nodeFactory, ptsGraph,
                            constraintGraph);
            }

            if (mergeSelf) {
              collapseNodes(ctRep, node, nodeFactory, ptsGraph,
                            constraintGraph);
              // If the node collapsing succeeds, we can't proceed here because
              // node no longer exists. Push ctRep to the worklist and proceed
              if (ctRep != node) {
                nextWorkList->enqueue(ctRep);
                deltaPts[ctRep] = ptsGraph[ctRep];
                delta.clear();
                continue;
              }
            }
          }
        }

        const bool hasLoads  = cNode->load_begin()  != cNode->load_end();
        const bool hasStores = cNode->store_begin() != cNode->store_end();
        const bool hasGeps = cNode->gep_begin() != cNode->gep_end();

        if (hasLoads || hasStores || hasGeps) {
          for (auto v : delta) {
            NodeIndex vRep = nodeFactory.getMergeTarget(v);
            for (auto dst : cNode->loads()) {
              NodeIndex tgtNode = nodeFactory.getMergeTarget(dst);
              // errs() << "Examining load edge " << node << " -> " << tgtNode <<
              // "\n";
              if (constraintGraph.insertCopyEdge(vRep, tgtNode)) {
                // errs() << "\tInsert copy edge " << v << " -> " << tgtNode <<
                // "\n";
                auto vRepPtsItr = ptsGraph.find(vRep);
                if (vRepPtsItr != ptsGraph.end() && !vRepPtsItr->second.isEmpty()) {
                    AndersPtsSet newPts;
                    if (vRepPtsItr->second.subtract(ptsGraph[tgtNode], newPts)) {
                        ptsGraph[tgtNode].unionWith(newPts);
                        deltaPts[tgtNode].unionWith(newPts);
                        nextWorkList->enqueue(tgtNode);
                    }
                }
              }

              // If we find that dst has been merged to elsewhere, remember this
              // fact to update the constraint graph later
              if (tgtNode != dst)
                updateMap[dst] = tgtNode;
            }

            for (auto const &dst : cNode->stores()) {
              NodeIndex tgtNode = nodeFactory.getMergeTarget(dst);
              if (constraintGraph.insertCopyEdge(tgtNode, vRep)) {
                // errs() << "\tInsert copy edge " << tgtNode << " -> " << v <<
                // "\n";
                auto tgtPtsItr = ptsGraph.find(tgtNode);
                if (tgtPtsItr != ptsGraph.end() && !tgtPtsItr->second.isEmpty()) {
                    AndersPtsSet newPts;
                    if (tgtPtsItr->second.subtract(ptsGraph[vRep], newPts)) {
                        ptsGraph[vRep].unionWith(newPts);
                        deltaPts[vRep].unionWith(newPts);
                        nextWorkList->enqueue(vRep);
                    }
                }
              }

              // If we find that dst has been merged to elsewhere, remember this
              // fact to update the constraint graph later
              if (tgtNode != dst)
                updateMap[dst] = tgtNode;
            }

            for (auto const &gep : cNode->geps()) {
              NodeIndex fieldObj = nodeFactory.getOrCreateFieldObject(vRep, gep.fields);
              AndersPtsSet& dstPts = ptsGraph[gep.dst];
              if (!dstPts.has(fieldObj)) {
                dstPts.insert(fieldObj);
                deltaPts[gep.dst].insert(fieldObj);
                nextWorkList->enqueue(gep.dst);
              }
            }
          }
        }

        // Now perform the load edge updates
        for (auto const &mapping : updateMap)
          cNode->replaceLoadEdge(mapping.first, mapping.second);

        // Now perform the store edge updates
        for (auto const &mapping : updateMap)
          cNode->replaceStoreEdge(mapping.first, mapping.second);

        for (auto const &mapping : updateMap)
          cNode->replaceGepEdge(mapping.first, mapping.second);
        updateMap.clear();

        // Finally, it's time to propagate pts-to info along the copy edges
        for (auto const &dst : *cNode) {
          NodeIndex tgtNode = nodeFactory.getMergeTarget(dst);
          if (node == tgtNode)
            continue;
          AndersPtsSet &tgtPtsSet = ptsGraph[tgtNode];

          // errs() << "pts[" << tgtNode << "] |= pts[" << node << "]\n";
          // bool isChanged = tgtPtsSet.unionWith(delta);
          AndersPtsSet newPts;

          if (delta.subtract(tgtPtsSet, newPts)) {
            tgtPtsSet.unionWith(newPts);
            deltaPts[tgtNode].unionWith(newPts);
            nextWorkList->enqueue(tgtNode);
          } else if (EnableLCD) {
            // This is where we do lazy cycle detection.
            // If this is a cycle candidate (equal points-to sets and this
            // particular edge has not been cycle-checked previously), add to
            // the list to check for cycles on the next iteration
            // auto edgePair = std::make_pair(node, tgtNode);
            uint64_t edge = encodeEdge(node, tgtNode);
            // if (!checkedEdges.count(edgePair) && ptsSet == tgtPtsSet) {
            if (!checkedEdges.count(edge)) {
              checkedEdges.insert(edge);
              cycleCandidates.insert(tgtNode);
            }
          }

          if (tgtNode != dst)
            updateMap[dst] = tgtNode;
        }

        // Now perform the copy edge updates
        for (auto const &mapping : updateMap)
          cNode->replaceCopyEdge(mapping.first, mapping.second);
        updateMap.clear();
      }
      delta.clear();
    }
    // Swap the current and the next worklist
    std::swap(currWorkList, nextWorkList);
  }
  auto e = std::chrono::steady_clock::now();
  std::chrono::duration<double> elapsed = (e-s);
  std::cout << "constraint solver, elapsed: " << elapsed.count() << "\n";
}
