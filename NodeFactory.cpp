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

using namespace llvm;

constexpr unsigned AndersNodeFactory::InvalidIndex = ~0u;

AndersNodeFactory::AndersNodeFactory() {
  // Note that we can't use std::vector::emplace_back() here because
  // AndersNode's constructors are private hence std::vector cannot see it

  // Node #0 is always the universal ptr: the ptr that we don't know anything
  // about.
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, 0));
  // Node #0 is always the universal obj: the obj that we don't know anything
  // about.
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, 1));
  // Node #2 always represents the null pointer.
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, 2));
  // Node #3 is the object that null pointer points to
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, 3));

  assert(nodes.size() == 4);
}

NodeIndex AndersNodeFactory::createValueNode(const Value *val, FieldType fields) {
  unsigned nextIdx = nodes.size();
  if (val != nullptr) {
    assert(!valueNodeMap.contains(val, fields) &&
           "Trying to insert two mappings to valueNodeMap!");
    valueNodeMap[{val, fields}] = nextIdx;
  }
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, nextIdx, val, fields));
  return nextIdx;
}

NodeIndex AndersNodeFactory::createObjectNode(const Value *val, FieldType fields) {
  unsigned nextIdx = nodes.size();
  if (val != nullptr) {
    assert(!objNodeMap.contains(val, fields) &&
           "Trying to insert two mappings to objNodeMap!");
    objNodeMap[{val, fields}] = nextIdx;
  }

  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, nextIdx, val, fields));
  return nextIdx;
}

NodeIndex AndersNodeFactory::createReturnNode(const llvm::Function *f) {
  auto existing = returnMap.find(f);
  if (existing != returnMap.end()) return existing->second;

  unsigned nextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, nextIdx, f));
  returnMap[f] = nextIdx;
  return nextIdx;
}

NodeIndex AndersNodeFactory::createVarargNode(const llvm::Function *f) {
  unsigned nextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, nextIdx, f));

  assert(!varargMap.count(f) && "Trying to insert two mappings to varargMap!");
  varargMap[f] = nextIdx;

  return nextIdx;
}

NodeIndex AndersNodeFactory::getValueNodeFor(const Value *val, FieldType fields) {
  if (const Constant *c = dyn_cast<Constant>(val)) {
    if (!isa<GlobalValue>(c))
      return getValueNodeForConstant(c, fields);
  }
  return valueNodeMap.find(val, fields);
}

NodeIndex AndersNodeFactory::getValueNodeForConstant(const llvm::Constant *c, FieldType fields) {
  assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");

  if (isa<ConstantPointerNull>(c) || isa<UndefValue>(c))
    return getNullPtrNode();
  else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c))
    return getValueNodeFor(gv, fields);
  else if (const ConstantExpr *ce = dyn_cast<ConstantExpr>(c)) {
    switch (ce->getOpcode()) {
    // Pointer to any field within a struct is treated as a pointer to the first
    // field
    case Instruction::GetElementPtr: {
      FieldType fields = getFields(c);
      NodeIndex base = getValueNodeFor(c->getOperand(0), {});
      if (base == InvalidIndex)
          return InvalidIndex;
      NodeIndex existing = getValueNodeFor(c->getOperand(0), fields);
      if (existing != InvalidIndex)
          return existing;
      return createValueNode(c->getOperand(0), fields);
    }
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
      return createValueNode();
    case Instruction::BitCast:
      return getValueNodeForConstant(ce->getOperand(0), fields);
    default:
      errs() << "Constant Expr not yet handled: " << *ce << "\n";
      llvm_unreachable(0);
    }
  }

  llvm_unreachable("Unknown constant pointer!");
  return InvalidIndex;
}

NodeIndex AndersNodeFactory::getObjectNodeFor(const Value *val, FieldType fields) const {
  if (const Constant *c = dyn_cast<Constant>(val))
    if (!isa<GlobalValue>(c))
      return getObjectNodeForConstant(c, fields);
  return objNodeMap.find(val, fields);
}

NodeIndex
AndersNodeFactory::getObjectNodeForConstant(const llvm::Constant *c, FieldType fields) const {
  assert(isa<PointerType>(c->getType()) && "Not a constant pointer!");

  if (isa<ConstantPointerNull>(c))
    return getNullObjectNode();
  else if (const GlobalValue *gv = dyn_cast<GlobalValue>(c))
    return getObjectNodeFor(gv, fields);
  else if (const ConstantExpr *ce = dyn_cast<ConstantExpr>(c)) {
    switch (ce->getOpcode()) {
    // Pointer to any field within a struct is treated as a pointer to the first
    // field
    case Instruction::GetElementPtr:
      return getObjectNodeForConstant(ce->getOperand(0), fields);
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
      return getUniversalObjNode();
    case Instruction::BitCast:
      return getObjectNodeForConstant(ce->getOperand(0), fields);
    default:
      errs() << "Constant Expr not yet handled: " << *ce << "\n";
      llvm_unreachable(0);
    }
  }

  llvm_unreachable("Unknown constant pointer!");
  return InvalidIndex;
}

NodeIndex AndersNodeFactory::getReturnNodeFor(const llvm::Function *f) const {
  auto itr = returnMap.find(f);
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
llvm::SmallVector<unsigned int, 4> AndersNodeFactory::getFields(const llvm::Value *v) const {
  return valueNodeMap.getFields(v);
}

std::vector<FieldType> AndersNodeFactory::lookupFields(AndersNode::AndersNodeType type, const llvm::Value *v) const {
  return type == AndersNode::VALUE_NODE ?
    valueNodeMap.lookupFields(v) :
    objNodeMap.lookupFields(v);
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

void AndersNodeFactory::getAllocSites(std::vector<const llvm::Value *> &allocSites) const {
  allocSites.clear();
  allocSites.reserve(objNodeMap.size());
  // for (auto const &mapping : objNodeMap)
    // allocSites.push_back(mapping.first);
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
    errs() << mapping.first->getName() << "  -->>  [Node #" << mapping.second
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
