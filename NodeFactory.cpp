#include "NodeFactory.h"
#include "NodeMap.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace llvm;

constexpr unsigned AndersNodeFactory::InvalidIndex = ~0u;
constexpr unsigned AndersNodeFactory::GlobalContextID = ~0u;

AndersNodeFactory::AndersNodeFactory() {
  // Note that we can't use std::vector::emplace_back() here because
  // AndersNode's constructors are private hence std::vector cannot see it

  // Node #0 is always the universal ptr: the ptr that we don't know anything
  // about.
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, GlobalContextID, 0));
  // Node #0 is always the universal obj: the obj that we don't know anything
  // about.
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, GlobalContextID, 1));
  // Node #2 always represents the null pointer.
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, GlobalContextID, 2));
  // Node #3 is the object that null pointer points to
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, GlobalContextID, 3));

  assert(nodes.size() == 4);
}

NodeIndex AndersNodeFactory::createValueNode(const Context *context, const Value *val, FieldType fields) {
  unsigned contextId = context != nullptr ? context->id : GlobalContextID;
  unsigned nextIdx = nodes.size();
  if (val != nullptr) {
    assert(!valueNodeMap.contains(context, val, fields) &&
           "Trying to insert two mappings to valueNodeMap!");
    valueNodeMap[{context, val, fields}] = nextIdx;
  }
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, contextId, nextIdx, val, fields));
  return nextIdx;
}

NodeIndex AndersNodeFactory::createObjectNode(const Context *context, const Value *val, FieldType fields) {
  unsigned contextId = context != nullptr ? context->id : GlobalContextID;
  unsigned nextIdx = nodes.size();
  if (val != nullptr) {
    assert(!objNodeMap.contains(context, val, fields) &&
           "Trying to insert two mappings to objNodeMap!");
    objNodeMap[{context, val, fields}] = nextIdx;
  }

  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, contextId, nextIdx, val, fields));
  return nextIdx;
}

NodeIndex AndersNodeFactory::createReturnNode(const Context *context, const llvm::Function *f) {
  auto existing = returnMap.find({context, f});
  if (existing != returnMap.end()) return existing->second;

  unsigned contextId = context != nullptr ? context->id : GlobalContextID;
  unsigned nextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, contextId, nextIdx, f));
  returnMap[{context, f}] = nextIdx;
  return nextIdx;
}

NodeIndex AndersNodeFactory::createVarargNode(const llvm::Function *f) {
  unsigned nextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, GlobalContextID, nextIdx, f));

  assert(!varargMap.count(f) && "Trying to insert two mappings to varargMap!");
  varargMap[f] = nextIdx;

  return nextIdx;
}

NodeIndex AndersNodeFactory::getValueNodeFor(const Context *context, const Value *val, FieldType fields) {
  if (const Constant *c = dyn_cast<Constant>(val)) {
    if (!isa<GlobalValue>(c))
      return getValueNodeForConstant(context, c, fields);
    else
      context = _globalCtx;
  }
  return valueNodeMap.find(context, val, fields);
}

NodeIndex AndersNodeFactory::getValueNodeForConstant(const Context *context, const llvm::Constant *c, FieldType fields) {
  assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");

  if (isa<ConstantPointerNull>(c) || isa<UndefValue>(c))
    return getNullPtrNode();
  else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c))
    return getValueNodeFor(context, gv, fields);
  else if (const ConstantExpr *ce = dyn_cast<ConstantExpr>(c)) {
    switch (ce->getOpcode()) {
    // Pointer to any field within a struct is treated as a pointer to the first
    // field
    case Instruction::GetElementPtr: {
      FieldType fields = getFields(context, c);
      NodeIndex base = getValueNodeFor(context, c->getOperand(0), {});
      if (base == InvalidIndex)
          return InvalidIndex;
      NodeIndex existing = getValueNodeFor(context, c->getOperand(0), fields);
      if (existing != InvalidIndex)
          return existing;
      return createValueNode(context, c->getOperand(0), fields);
    }
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
      return createValueNode(context);
    case Instruction::BitCast:
      return getValueNodeForConstant(context, ce->getOperand(0), fields);
    default:
      errs() << "Constant Expr not yet handled: " << *ce << "\n";
      llvm_unreachable(0);
    }
  }

  llvm_unreachable("Unknown constant pointer!");
  return InvalidIndex;
}

NodeIndex AndersNodeFactory::getObjectNodeFor(const Context *context, const Value *val, FieldType fields) const {
  if (const Constant *c = dyn_cast<Constant>(val))
    if (!isa<GlobalValue>(c))
      return getObjectNodeForConstant(context, c, fields);
  return objNodeMap.find(context, val, fields);
}

NodeIndex
AndersNodeFactory::getObjectNodeForConstant(const Context *context, const llvm::Constant *c, FieldType fields) const {
  assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");

  if (isa<ConstantPointerNull>(c))
    return getNullObjectNode();
  else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c))
    return getObjectNodeFor(context, gv, fields);
  else if (const ConstantExpr *ce = dyn_cast<ConstantExpr>(c)) {
    switch (ce->getOpcode()) {
    // Pointer to any field within a struct is treated as a pointer to the first
    // field
    case Instruction::GetElementPtr:
      return getObjectNodeForConstant(context, ce->getOperand(0), fields);
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
      return getUniversalObjNode();
    case Instruction::BitCast:
      return getObjectNodeForConstant(context, ce->getOperand(0), fields);
    default:
      errs() << "Constant Expr not yet handled: " << *ce << "\n";
      llvm_unreachable(0);
    }
  }

  llvm_unreachable("Unknown constant pointer!");
  return InvalidIndex;
}

NodeIndex AndersNodeFactory::getReturnNodeFor(const Context *context, const llvm::Function *f) const {
  auto itr = returnMap.find({context, f});
  return itr != returnMap.end()
    ? itr->second
    : InvalidIndex;
}

NodeIndex AndersNodeFactory::getVarargNodeFor(const llvm::Function *f) const {
  auto itr = varargMap.find(f);
  return itr != varargMap.end()
    ? itr->second
    : InvalidIndex;
}

/*
 * [deprecated, use lookupFields]
*/
llvm::SmallVector<unsigned int, 4> AndersNodeFactory::getFields(const Context *ctx, const llvm::Value *v) const {
  return valueNodeMap.getFields(ctx, v);
}

std::vector<FieldType> AndersNodeFactory::lookupFields(AndersNode::AndersNodeType type, const Context *ctx, const llvm::Value *v) const {
  return type == AndersNode::VALUE_NODE ?
    valueNodeMap.lookupFields(ctx, v) :
    objNodeMap.lookupFields(ctx, v);
}

void AndersNodeFactory::mergeNode(NodeIndex n0, NodeIndex n1) {
  assert(n0 < nodes.size() && n1 < nodes.size());
  nodes[n1].mergeTarget = n0;
}

NodeIndex AndersNodeFactory::getMergeTarget(NodeIndex n) {
  assert(n < nodes.size());
  NodeIndex ret = nodes[n].mergeTarget;
  if (ret != n) {
    std::vector<NodeIndex> path(1, n);
    while (ret != nodes[ret].mergeTarget) {
      path.push_back(ret);
      ret = nodes[ret].mergeTarget;
    }
    for (auto idx : path)
      nodes[idx].mergeTarget = ret;
  }
  assert(ret < nodes.size());
  return ret;
}

NodeIndex AndersNodeFactory::getMergeTarget(NodeIndex n) const {
  assert(n < nodes.size());
  NodeIndex ret = nodes[n].mergeTarget;
  while (ret != nodes[ret].mergeTarget)
    ret = nodes[ret].mergeTarget;
  return ret;
}

void AndersNodeFactory::getAllocSites(
    std::vector<std::pair<const Context*, const llvm::Value *>> &allocSites) const {
  allocSites.clear();
  allocSites.reserve(objNodeMap.size());
  for (auto const &mapping : objNodeMap)
    allocSites.push_back({std::get<0>(mapping.first), std::get<1>(mapping.first)});
}

void AndersNodeFactory::dumpNode(NodeIndex idx) const {
  const AndersNode n = nodes.at(idx);
  if (n.type == AndersNode::VALUE_NODE)
    errs() << "[V ";
  else if (n.type == AndersNode::OBJ_NODE)
    errs() << "[O ";
  else
    assert(false && "Wrong type number!");
  errs() << "\033[38;2;174;245;184m#" << n.idx << "\033[0m";
  errs() << ", C \033[38;2;255;253;161m#" << n.contextId << "\033[0m";
  if (n.hasFields()) {
    errs() << ", Fields: ";
    n.printFields();
  }
  errs() << "]";
}

void AndersNodeFactory::dumpNodeInfo() const {
  errs() << "\n----- Print AndersNodeFactory Info -----\n";
  errs() << "Value Node Map:\n";

  for (auto const &node : nodes) {
    dumpNode(node.getIndex());
    errs() << ", val = ";
    const Value *val = node.getValue();
    if (val == nullptr)
      errs() << "nullptr";
    else if (isa<Function>(val))
      errs() << "  <func> " << val->getName();
    else
      errs() << *val;
    errs() << "\n";
  }

  errs() << "\nReturn Map:\n";
  for (auto const &mapping : returnMap)
    errs() << mapping.first.second->getName() << "  -->>  [Node #" << mapping.second
           << "]\n";
  errs() << "\nVararg Map:\n";
  for (auto const &mapping : varargMap)
    errs() << mapping.first->getName() << "  -->>  [Node #" << mapping.second
           << "]\n";
  errs() << "----- End of Print -----\n";
}

void AndersNodeFactory::dumpRepInfo() const {
  errs() << "\n----- Print Node Merge Info -----\n";
  for (NodeIndex i = 0, e = nodes.size(); i < e; ++i) {
    NodeIndex rep = getMergeTarget(i);
    if (rep != i)
      errs() << i << " -> " << rep << "\n";
  }
  errs() << "----- End of Print -----\n";
}

/**
 * Creates a new Context object given a previous context and callsite. 
*/
Context* AndersNodeFactory::createContext(Context* _prevCtx, const llvm::CallBase* callSite) {
  const llvm::Function *callee = callSite ? callSite->getCalledFunction() : nullptr;
  if (_prevCtx) {
    Context* existing = _prevCtx->getChild(callSite, callee);
    if (existing) return existing;
  }
  Context* context = new Context(_ctxCounter, _prevCtx, callSite, callee);
  _ctxCounter++;
  _contexts.push_back(context);
  if (callSite && _prevCtx)
      registerCallSiteContext(callSite, context);
  return context;
}

/**
 * Creates a new Context object given a previous context, a handling callsite, and a function.
 * This is used for cases where we manually connect contexts to a new one where the function has an obscure callsite.
*/
Context* AndersNodeFactory::createContext(Context* _prevCtx, const llvm::CallBase* callSite, const llvm::Function *f) {
  if (_prevCtx) {
    Context* existing = _prevCtx->getChild(callSite, f);
    if (existing) return existing;
  }
  Context* context = new Context(_ctxCounter, _prevCtx, callSite, f);
  context->func = f;
  _ctxCounter++;
  _contexts.push_back(context);
  if (callSite && _prevCtx)
      registerCallSiteContext(callSite, context);
  return context;
}

/**
 * Creates a new Context object without an associated previous context or callsite. 
*/
Context* AndersNodeFactory::createContext() {
  Context* context = new Context(_ctxCounter, nullptr, nullptr, nullptr);
  _ctxCounter++;
  _contexts.push_back(context);
  return context;
}

/**
 * Returns the global context (ie: context[0]).
 * Initial constraint collection for globals must already be completed. 
*/
const Context* AndersNodeFactory::getGlobalCtx() const {
  assert(_contexts.size() > 0);
  return _contexts[0];
}

void AndersNodeFactory::registerCallSiteContext(const llvm::CallBase* cs, Context* ctx) {
    auto &vec = _callSiteContexts[cs];
    if (std::find(vec.begin(), vec.end(), ctx) == vec.end())
        vec.push_back(ctx);
}

std::vector<Context*> AndersNodeFactory::getContextsForCallSite(const llvm::CallBase* cs) {
    auto it = _callSiteContexts.find(cs);
    if (it == _callSiteContexts.end()) return {};
    return it->second;
}

/*
 * Returns the Context* or nullptr given a valid ID.
*/
const Context* AndersNodeFactory::getContextByID(unsigned int ctxId) const {
  auto itr = std::find_if(_contexts.begin(), _contexts.end(), [ctxId](const Context *candidate) {
    return candidate->id == ctxId;
  });
  if (itr == _contexts.end()) return nullptr;
  return *itr;
}

/*
 * Returns the Context given a CallBase (CallInst, InvokeInst)
*/
const Context* AndersNodeFactory::getContext(const llvm::Value *v) const {
  const CallBase *callBase = dyn_cast<CallBase>(v);
  if (!callBase) return nullptr;

  auto itr = std::find_if(_contexts.begin(), _contexts.end(), [callBase](const Context *candidate) {
    return candidate->callSite == callBase;
  });
  if (itr == _contexts.end()) return nullptr;
  return *itr;
}


/**
 * Given a nodeId, return a list of all associated contexts.
*/
std::vector<const Context*> AndersNodeFactory::getAssociatedContexts(NodeIndex n) const {
  return valueNodeMap.getAssociatedContexts(n);
}

/**
 * Given a Value*, return a list of all associated contexts.
*/
std::vector<const Context*> AndersNodeFactory::getAssociatedContexts(const Value *value) const {
  return valueNodeMap.getAssociatedContexts(value);
}

unsigned int AndersNodeFactory::getNumContexts() {
  return _contexts.size();
}

void AndersNodeFactory::setDataLayout(const DataLayout *layout) {
  valueNodeMap.setDataLayout(layout);
  objNodeMap.setDataLayout(layout);
  _layout = layout;
}

const DataLayout* AndersNodeFactory::getDataLayout() const {
  return _layout;
}

NodeIndex AndersNodeFactory::getOrCreateFieldObject(NodeIndex baseObj, const FieldType& fields) {
    baseObj = getMergeTarget(baseObj);
    auto key = std::make_pair(baseObj, fields);
    auto it = fieldObjectMap.find(key);
    if (it != fieldObjectMap.end())
        return it->second;


    NodeIndex fieldObj = createObjectNode(nullptr);
    fieldObjectMap[key] = fieldObj;
    fieldObjectBaseMap[fieldObj] = baseObj;
    return fieldObj;
}

NodeIndex AndersNodeFactory::getFieldBaseObject(NodeIndex fieldObj) const {
    auto it = fieldObjectBaseMap.find(fieldObj);
    if (it == fieldObjectBaseMap.end())
        return InvalidIndex;

    return getMergeTarget(it->second);
}
