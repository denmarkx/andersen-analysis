#ifndef ANDERSEN_NODE_FACTORY_H
#define ANDERSEN_NODE_FACTORY_H

#include "NodeMap.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Value.h"
#include <climits>
using namespace llvm;

#include <vector>
#include <utility>

struct Context {
  unsigned int id;
  Context* prevCtx;
  Context* nextCtx;
  const llvm::CallBase* callSite;
  const llvm::Function* func = nullptr;
  llvm::DenseMap<std::pair<const llvm::CallBase*, const Function*>, Context*> children;

  Context(unsigned int _id, Context* _prevCtx, const llvm::CallBase* _callSite, const llvm::Function *f) {
    id = _id;
    prevCtx = _prevCtx;
    callSite = _callSite;
    func = f;

    if (prevCtx)
      prevCtx->children[{_callSite, f}] = this;
  }

  Context* getChild(const llvm::CallBase *cs, const Function *f) const {
    auto it = children.find({cs, f});
    return it != children.end() ? it->second : nullptr;
  }

  std::string str() {
    return "Context[" + std::to_string(id) + "]";
  }

  void printChain(size_t indent=0) const {
    if (!callSite) return;
    for (unsigned int i=0; i < indent; i++)
      errs() << " ";
    if (func)
      errs() << "[Handling Callsite for: " << func->getName() << "\n";
    errs() << *callSite << "\n";
    if (prevCtx)
      prevCtx->printChain(indent + 4);
  }
};

// AndersNode class - This class is used to represent a node in the constraint
// graph.  Due to various optimizations, it is not always the case that there is
// always a mapping from a Node to a Value. (In particular, we add artificial
// Node's that represent the set of pointed-to variables shared for each
// location equivalent Node. Ordinary clients are not allowed to create
// AndersNode objects. To guarantee index consistency, AndersNodes (and its
// subclasses) instances should only be created through AndersNodeFactory.
typedef unsigned NodeIndex;
class AndersNode {
public:
  enum AndersNodeType { VALUE_NODE, OBJ_NODE };

private:
  unsigned int contextId;
  AndersNodeType type;
  NodeIndex idx, mergeTarget;
  const llvm::Value *value;
  llvm::SmallVector<unsigned int, 4> _fields;

public:
  AndersNode(AndersNodeType t, unsigned ctxId, unsigned i,
    const llvm::Value *v = nullptr, llvm::SmallVector<unsigned int, 4> fields={})
      : type(t), contextId(ctxId), idx(i), mergeTarget(i), value(v), _fields(fields) {}

  NodeIndex getIndex() const { return idx; }
  const llvm::Value *getValue() const { return value; }

  void printFields() const {
    errs() << "[";
    unsigned int fieldsSize = _fields.size();
    for (unsigned int i=0; i < fieldsSize-1; i++)
      errs() << _fields[i] << ", ";
    if (fieldsSize > 0)
      errs() << _fields[fieldsSize-1];
    errs() << "]";
  }

  bool hasFields() const {
    return _fields.size() > 0;
  }

  friend class AndersNodeFactory;
};

// This is the factory class of AndersNode
// It use a vectors to hold all Nodes in the program
// Since vectors may invalidate all element pointers/iterators when resizing, it
// is impossible to return AndersNode* in public interfaces without using
// std::unique_ptr and heap allocations. Therefore, we use plain integers to
// represent nodes for public functions like createXXX and getXXX. This is ugly,
// but it is efficient.
typedef llvm::DenseMap<std::pair<const Context*, const llvm::Value*>, NodeIndex> NodeMapType;

class AndersNodeFactory {
public:
  // The largest unsigned int is reserved for invalid index
  static const unsigned InvalidIndex;
  static const unsigned GlobalContextID;

private:
  // The set of nodes
  std::vector<AndersNode> nodes;

  // Some special indices
  static const NodeIndex UniversalPtrIndex = 0;
  static const NodeIndex UniversalObjIndex = 1;
  static const NodeIndex NullPtrIndex = 2;
  static const NodeIndex NullObjectIndex = 3;

  // valueNodeMap - This map indicates the AndersNode* that a particular Value*
  // corresponds to
  NodeMap valueNodeMap;

  // ObjectNodes - This map contains entries for each memory object in the
  // program: globals, alloca's and mallocs. We are able to represent them as
  // llvm::Value* because we're modeling the heap with the simplest
  // allocation-site approach
  NodeMap objNodeMap;

  // returnMap - This map contains an entry for each function in the program
  // that returns a ptr.
  llvm::DenseMap<std::pair<const Context*, const llvm::Function *>, NodeIndex> returnMap;

  // varargMap - This map contains the entry used to represent all pointers
  // passed through the varargs portion of a function call for a particular
  // function.  An entry is not present in this map for functions that do not
  // take variable arguments.
  llvm::DenseMap<const llvm::Function *, NodeIndex> varargMap;

  DenseMap<std::pair<NodeIndex,FieldType>, NodeIndex> fieldObjectMap;
  DenseMap<NodeIndex, NodeIndex> fieldObjectBaseMap;

  std::vector<const Context*> _contexts;
  std::unordered_map<const llvm::CallBase*, std::vector<Context*>> _callSiteContexts;

  unsigned int _ctxCounter = 0;

public:
  AndersNodeFactory();

  Context* _globalCtx;

  // Factory methods
  NodeIndex createValueNode(const Context *context = nullptr, const llvm::Value *val = nullptr, FieldType fields={});
  NodeIndex createObjectNode(const Context *context = nullptr, const llvm::Value *val = nullptr, FieldType fields={});
  NodeIndex createReturnNode(const Context *context, const llvm::Function *f);
  NodeIndex createVarargNode(const llvm::Function *f);

  // Map lookup interfaces (return InvalidIndex if value not found)
  NodeIndex getValueNodeFor(const Context *context, const llvm::Value *val, FieldType fields={});
  NodeIndex getValueNodeForConstant(const Context *context, const llvm::Constant *c, FieldType fields={});
  NodeIndex getObjectNodeFor(const Context *context, const llvm::Value *val, FieldType fields={}) const;
  NodeIndex getObjectNodeForConstant(const Context *context, const llvm::Constant *c, FieldType fields={}) const;
  NodeIndex getReturnNodeFor(const Context *context, const llvm::Function *f) const;
  NodeIndex getVarargNodeFor(const llvm::Function *f) const;
  NodeIndex getOrCreateFieldObject(NodeIndex baseObj, const FieldType& fields);
  NodeIndex getFieldBaseObject(NodeIndex fieldObj) const;

  // [deprecated] - use lookupFields
  llvm::SmallVector<unsigned int, 4> getFields(const Context *ctx, const llvm::Value *v) const;

  std::vector<FieldType> lookupFields(AndersNode::AndersNodeType type, const Context *ctx, const llvm::Value *v) const;

  // Node merge interfaces
  void mergeNode(NodeIndex n0, NodeIndex n1); // Merge n1 into n0
  NodeIndex getMergeTarget(NodeIndex n);
  NodeIndex getMergeTarget(NodeIndex n) const;

  // Pointer arithmetic
  bool isObjectNode(NodeIndex i) const {
    return (nodes.at(i).type == AndersNode::OBJ_NODE);
  }
  NodeIndex getOffsetObjectNode(NodeIndex n, unsigned offset) const {
    assert(isObjectNode(n + offset));
    return n + offset;
  }

  // Special node getters
  NodeIndex getUniversalPtrNode() const { return UniversalPtrIndex; }
  NodeIndex getUniversalObjNode() const { return UniversalObjIndex; }
  NodeIndex getNullPtrNode() const { return NullPtrIndex; }
  NodeIndex getNullObjectNode() const { return NullObjectIndex; }

  // Value getters
  const llvm::Value *getValueForNode(NodeIndex i) const {
    return nodes.at(i).getValue();
  }
  void getAllocSites(std::vector<std::pair<const Context*, const llvm::Value *>> &) const;

  // Value remover
  void removeNodeForValue(const Context *context, const llvm::Value *val) { 
    valueNodeMap.erase(context, val); 
  }

  // Size getters
  unsigned getNumNodes() const { return nodes.size(); }

  // Context management:
  Context* createContext(Context* _prevCtx, const llvm::CallBase* callSite);
  Context* createContext(Context* _prevCtx, const llvm::CallBase* callSite, const llvm::Function *f);
  Context* createContext();

  void registerCallSiteContext(const llvm::CallBase* cs, Context* ctx);
  std::vector<Context*> getContextsForCallSite(const llvm::CallBase* cs);

  const Context* getGlobalCtx() const;
  const Context* getContext(unsigned int id) const;
  const Context* getContextByID(unsigned int ctxId) const;
  const Context* getContext(const llvm::Value *v) const;
  unsigned int getNumContexts();

  std::vector<const Context*> getAssociatedContexts(NodeIndex n) const;
  std::vector<const Context*> getAssociatedContexts(const Value* val) const;

  void setDataLayout(const DataLayout *layout);
  const DataLayout* getDataLayout() const;

  const DataLayout *_layout = nullptr;

  // For debugging purpose
  void dumpNode(NodeIndex) const;
  void dumpNodeInfo() const;
  void dumpRepInfo() const;
};

#endif
