#include "NodeFactory.h"
#include "NodeMapUtil.h"
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

NodeIndex AndersNodeFactory::createValueNode(const Value *val, FieldType fields, bool isDerived) {
  unsigned nextIdx = nodes.size();
  if (val != nullptr) {
    assert(!valueNodeMap.contains(val, fields) &&
           "Trying to insert two mappings to valueNodeMap!");
    valueNodeMap.insert(val, fields, nextIdx);
  }
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, nextIdx, val, fields));
  if (!isDerived)
    createDerivedValueNode(val, nextIdx);
  return nextIdx;
}

/*
 * For a value node whose value is an aggregate that contains a pointer,
 * this eagerly iterates through the aggregate type and creates derived node indices.
 * The original value is instead treated as a symbolic "base" value.
 *
 * Note: This is solving a different problem within field-sensitivity and has
 *       very little to do with GEPs, which is why this is exclusive to values only.
 *       Additionally, this is only for first-class aggregates: structs and arrays.
*/
void AndersNodeFactory::createDerivedValueNode(const Value *base, NodeIndex baseIdx, const Type* baseType) {
  if (!base) return;
  const Type *type = (baseType != nullptr) ? baseType : base->getType();

  if (!type->isAggregateType() || !NodeMapUtil::aggregateContainsPointer(type))
    return;

  SmallVector<NodeIndex, 4> childIdxs;
  for (FieldType &fields : NodeMapUtil::getAggregateFields(type)) {
    // If fields is strictly [0], then we use the base.
    if (fields.size() == 1 && fields[0] == 0) {
      // Technically, this is still a "child", but it's just the same thing.
      childIdxs.push_back(baseIdx);
      continue;
    };

    NodeIndex childIdx = createValueNode(base, fields, true);
    childIdxs.push_back(childIdx);
  }

  registerBaseAggregate(baseIdx, childIdxs);
  return;
}

NodeIndex AndersNodeFactory::createObjectNode(const Value *val, FieldType fields) {
  unsigned nextIdx = nodes.size();
  if (val != nullptr) {
    assert(!objNodeMap.contains(val, fields) &&
           "Trying to insert two mappings to objNodeMap!");
    objNodeMap.insert(val, fields, nextIdx);
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

  // If f (fields={}) doesn't exist in valuenodemap, we add it.
  // This is only because this return node is mostly symbolic
  // and isn't indicative of the actual function, nor is it an actual SSA value on its own.
  if (!valueNodeMap.contains(f, {}))
    valueNodeMap.insert(f, {}, nextIdx);

  // These are a bit special since they can return aggregate ptrs:
  // We send an explicit base type as the func's ret type, but the base is still f.
  createDerivedValueNode(f, nextIdx, f->getReturnType());
  return nextIdx;
}

NodeIndex AndersNodeFactory::createVarargNode(const llvm::Function *f) {
  unsigned nextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, nextIdx, f));

  assert(!varargMap.count(f) && "Trying to insert two mappings to varargMap!");
  varargMap[f] = nextIdx;

  return nextIdx;
}

void AndersNodeFactory::registerBaseAggregate(NodeIndex base, llvm::SmallVector<NodeIndex, 4> children) {
  assert(!_baseAggregateMap.contains(base) && "Trying to register base aggregate more than once.");
  _baseAggregateMap[base] = std::move(children);
}

bool AndersNodeFactory::isBaseAggregate(NodeIndex base) const {
  return _baseAggregateMap.contains(base);
}

const llvm::ArrayRef<NodeIndex> AndersNodeFactory::getAggregateChildren(NodeIndex base) const {
  if (!_baseAggregateMap.contains(base)) return {};
  return _baseAggregateMap.at(base);
}

const llvm::SmallVector<NodeIndex, 4>& AndersNodeFactory::getFields(NodeIndex idx) const {
  assert(idx < nodes.size() && "Invalid node index sent to getFields.");
  return nodes[idx].getFields();
}

NodeIndex AndersNodeFactory::getValueNodeFor(const Value *val, FieldType fields) {
  if (const Constant *c = dyn_cast<Constant>(val)) {
    if (!isa<GlobalValue>(c)) 
      return getValueNodeForConstant(c, fields);
  }
  return valueNodeMap.get(val, fields);
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
      FieldType fields = NodeMapUtil::getFields(c);
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
  return objNodeMap.get(val, fields);
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

NodeIndex AndersNodeFactory::getOrCreateFieldObject(NodeIndex baseObj, const FieldType& fields) {
    const Value *base = getValueForNode(baseObj);
    assert(base != nullptr);

    baseObj = getMergeTarget(baseObj);

    if (objNodeMap.contains(base, fields))
      return objNodeMap.get(base, fields);

    NodeIndex fieldObj = createObjectNode(base, fields);
    fieldObjectBaseMap[fieldObj] = baseObj;
    return fieldObj;
}

NodeIndex AndersNodeFactory::getFieldBaseObject(NodeIndex fieldObj) const {
    auto it = fieldObjectBaseMap.find(fieldObj);
    if (it == fieldObjectBaseMap.end())
        return InvalidIndex;

    return getMergeTarget(it->second);
}
