#include "Andersen.h"
#include "Constraint.h"
#include "NodeFactory.h"

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"

#include <queue>

using namespace llvm;

// CollectConstraints - This stage scans the program, adding a constraint to the
// Constraints list for each instruction in the program that induces a
// constraint, and setting up the initial points-to graph.

void Andersen::collectConstraints(const Module &M) {
  // First, the universal ptr points to universal obj, and the universal obj
  // points to itself
  constraints.emplace_back(AndersConstraint::ADDR_OF,
                           nodeFactory.getUniversalPtrNode(),
                           nodeFactory.getUniversalObjNode());

  // Next, the null pointer points to the null object.
  constraints.emplace_back(AndersConstraint::ADDR_OF,
                           nodeFactory.getNullPtrNode(),
                           nodeFactory.getNullObjectNode());

  // Next, add any constraints on global variables. Associate the address of the
  // global object as pointing to the memory for the global: &G = <G memory>
  collectConstraintsForGlobals(M);

  // Here is a notable point before we proceed:
  // For functions with non-local linkage type, theoretically we should not
  // trust anything that get passed to it or get returned by it. However,
  // precision will be seriously hurt if we do that because if we do not run a
  // -internalize pass before the -anders pass, almost every function is marked
  // external. We'll just assume that even external linkage will not ruin the
  // analysis result first

  for (auto const &f : M)
    scanFunction(&f);
}

static bool typeContainsPointer(const Type *t) {
    if (t->isPointerTy()) return true;
    if (t->isArrayTy()) return typeContainsPointer(t->getArrayElementType());
    if (t->isStructTy()) {
        for (unsigned i = 0; i < t->getStructNumElements(); ++i)
            if (typeContainsPointer(t->getStructElementType(i))) return true;
    }
    return false;
}

void Andersen::scanFunction(const llvm::Function *f) {
  // First, create a value node for each instruction with pointer type. It is
  // necessary to do the job here rather than on-the-fly because an
  // instruction may refer to the value node defined before it (e.g. phi
  // nodes)
  for (const_inst_iterator itr = inst_begin(f), ite = inst_end(f); itr != ite;
       ++itr) {
    auto inst = &*itr.getInstructionIterator();
    if (inst->getType()->isPointerTy()) {
        nodeFactory.createValueNode(inst);
    }
    // i want to say this can be simplified somehow
    else if (isa<CallBase>(inst) && typeContainsPointer(inst->getType()))
        nodeFactory.createValueNode(inst);
    else if (isa<InsertValueInst>(inst) && typeContainsPointer(inst->getType()))
      nodeFactory.createValueNode(inst);
    }
  
  // Now, collect constraint for each relevant instruction
  for (const_inst_iterator itr = inst_begin(f), ite = inst_end(f); itr != ite;
       ++itr) {
    auto inst = &*itr.getInstructionIterator();
    collectConstraintsForInstruction(inst);
  }
}

void Andersen::collectConstraintsForGlobals(const Module &M) {
  // Create a pointer and an object for each global variable
  for (auto const &globalVal : M.globals()) {
    NodeIndex gVal = nodeFactory.createValueNode(&globalVal);
    NodeIndex gObj = nodeFactory.createObjectNode(&globalVal);
    constraints.emplace_back(AndersConstraint::ADDR_OF, gVal, gObj);
  }

  // Aliases do not create new objects. Instead, they act as p = &q.
  for (auto const &alias : M.aliases()) {
    NodeIndex aliasIdx = nodeFactory.createValueNode(&alias);
    NodeIndex aliaseeIdx = nodeFactory.getObjectNodeFor(alias.getAliasee());
    constraints.emplace_back(AndersConstraint::ADDR_OF, aliasIdx, aliaseeIdx);
  }

  // Functions and function pointers are also considered global
  for (auto const &f : M) {
    setupFunctionConstraints(&f);
  }

  // Init globals here since an initializer may refer to a global var/func below it
  for (auto const &globalVal : M.globals()) {
    NodeIndex gObj = nodeFactory.getObjectNodeFor(&globalVal);
    assert(gObj != AndersNodeFactory::InvalidIndex &&
           "Cannot find global object!");

    if (globalVal.hasDefinitiveInitializer()) {
      addGlobalInitializerConstraints(gObj, globalVal.getInitializer());
    } else {
      // If it doesn't have an initializer (i.e. it's defined in another
      // translation unit), it points to the universal set.
      NodeIndex fObj = nodeFactory.createObjectNode();
      constraints.emplace_back(AndersConstraint::ADDR_OF, gObj, fObj);
    }
  }
}

void Andersen::setupFunctionConstraints(const Function *f) {
  // If f is an addr-taken function, create a pointer and an object for it
  if (f->hasAddressTaken()) {
    // 创建一个值节点和一个对象节点
    NodeIndex fVal = nodeFactory.createValueNode(f);
    NodeIndex fObj = nodeFactory.createObjectNode(f);
    constraints.emplace_back(AndersConstraint::ADDR_OF, fVal, fObj);
  }

  if (f->isDeclaration() || f->isIntrinsic()) return;

  // Create return node
  const Type *retTy = f->getFunctionType()->getReturnType();
  if (retTy->isPointerTy() || typeContainsPointer(retTy)) {
      nodeFactory.createReturnNode(f);
  }

  // Create vararg node
  if (f->getFunctionType()->isVarArg())
    nodeFactory.createVarargNode(f);

  // Add nodes for all formal arguments.
  for (Function::const_arg_iterator itr = f->arg_begin(), ite = f->arg_end();
       itr != ite; ++itr) {
    if (isa<PointerType>(itr->getType()))
      nodeFactory.createValueNode(&*itr);
  }
}

void Andersen::addGlobalInitializerConstraints(NodeIndex objNode,
                                               const Constant *c) {
  // errs() << "Called with node# = " << objNode << ", initializer = " << *c <<
  // "\n";
  if (c->getType()->isSingleValueType()) {
    if (isa<PointerType>(c->getType())) {
      NodeIndex rhsNode = nodeFactory.getObjectNodeForConstant(c);
      assert(rhsNode != AndersNodeFactory::InvalidIndex &&
             "rhs node not found");
      if (rhsNode == nodeFactory.getUniversalObjNode() ||
          rhsNode == AndersNodeFactory::InvalidIndex) {
          rhsNode = nodeFactory.createObjectNode();
      }
      constraints.emplace_back(AndersConstraint::ADDR_OF, objNode, rhsNode);
    }
  } else if (c->isNullValue()) {
    constraints.emplace_back(AndersConstraint::COPY, objNode,
                             nodeFactory.getNullObjectNode());
  } else if (!isa<UndefValue>(c)) {
    // Since we are doing field-insensitive analysis, all objects in the
    // array/struct are pointed-to by the 1st-field pointer
    assert(isa<ConstantArray>(c) || isa<ConstantDataSequential>(c) ||
           isa<ConstantStruct>(c));

    FieldType fields;
    addGlobalAggregateConstraints(nodeFactory.getValueForNode(objNode), c, fields);

  }
}

void Andersen::addGlobalAggregateConstraints(const llvm::Value *aggregate, const llvm::Constant *c, FieldType &fields) {
  for (unsigned int i=0; i < c->getNumOperands(); i++) {
    Constant *element = cast<Constant>(c->getOperand(i));
    FieldType newFields;
    newFields.append(fields);
    newFields.push_back(i);

    if (element->getType()->isAggregateType()) {
      addGlobalAggregateConstraints(aggregate, element, newFields);
      continue;
    }

    NodeIndex objIdx = nodeFactory.getObjectNodeFor(aggregate, newFields);
    if (objIdx == AndersNodeFactory::InvalidIndex)
      objIdx = nodeFactory.createObjectNode(aggregate, newFields);

    addGlobalInitializerConstraints(objIdx, element);
  }
}

void Andersen::collectConstraintsForInstruction(const Instruction *inst) {
  switch (inst->getOpcode()) {
  case Instruction::Alloca: {
    NodeIndex valNode = nodeFactory.getValueNodeFor(inst);
    assert(valNode != AndersNodeFactory::InvalidIndex &&
           "Failed to find alloca value node");
    NodeIndex objNode = nodeFactory.createObjectNode(inst);
    constraints.emplace_back(AndersConstraint::ADDR_OF, valNode, objNode);
    break;
  }
  case Instruction::Call:
  case Instruction::Invoke: {
    const CallBase *cb = dyn_cast<CallBase>(inst);
    assert(cb && "Something wrong with callsite?");
    addConstraintForCall(cb);
    break;
  }
  case Instruction::Ret: {
      if (inst->getNumOperands() == 0) break;
      const Type *retTy = inst->getOperand(0)->getType();
      if (!retTy->isPointerTy() && !typeContainsPointer(retTy)) break;

      NodeIndex retIndex = nodeFactory.getReturnNodeFor(
          inst->getParent()->getParent());
      if (retIndex == AndersNodeFactory::InvalidIndex) break;

      NodeIndex valIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
      if (valIndex == AndersNodeFactory::InvalidIndex) break;

      constraints.emplace_back(AndersConstraint::COPY, retIndex, valIndex);
      break;
  }
  case Instruction::Load: {
    if (inst->getType()->isPointerTy()) {
      NodeIndex opIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
      assert(opIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find load operand node");
      NodeIndex valIndex = nodeFactory.getValueNodeFor(inst);
      assert(valIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find load value node");
      constraints.emplace_back(AndersConstraint::LOAD, valIndex, opIndex);
    }
    break;
  }
  case Instruction::Store: {
    if (inst->getOperand(0)->getType()->isPointerTy()) {
      NodeIndex srcIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
      assert(srcIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find store src node");
      NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst->getOperand(1));
      assert(dstIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find store dst node");
      constraints.emplace_back(AndersConstraint::STORE, dstIndex, srcIndex);
    }
    break;
  }
  case Instruction::GetElementPtr: {
    assert(inst->getType()->isPointerTy());

    // P1 = getelementptr P2, ... --> <Copy/P1/P2>
    const llvm::Value *src = inst->getOperand(0);
    auto fields = nodeFactory.getFields(inst);

    NodeIndex srcIndex = nodeFactory.getValueNodeFor(src);
    NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);

    // If our source is a GEP, we need to resolve the alloc site.
    if (const GetElementPtrInst *sourceInst = dyn_cast<GetElementPtrInst>(src)) {
      NodeIndex underlyingSrcIndex = AndersNodeFactory::InvalidIndex;

      // We can assume that the source GEP has been resolved properly.
      // ..meaning it already has a constraint to an object:
      auto itr = std::find_if(constraints.begin(), constraints.end(), [&](const AndersConstraint& c) {
        return (c.getType() == AndersConstraint::ADDR_OF || c.getType() == AndersConstraint::GEP) && c.getDest() == srcIndex;
      });

      // Best case scenario is itr isnt the end..otherwise, we have to try and find it:
      if (itr != constraints.end() && itr->getType() != AndersConstraint::GEP)
        underlyingSrcIndex = itr->getSrc();
      else {
        std::queue<NodeIndex> worklist;
        worklist.push(itr->getSrc());

        while (!worklist.empty()) {
          NodeIndex current = worklist.front();
          worklist.pop();
          for (const AndersConstraint &c : constraints) {
            if (c.getDest() == current) {
              if (c.getType() == AndersConstraint::ADDR_OF) {
                // In this case, the destination is acceptable. Otherwise we'll be getting the object.
                underlyingSrcIndex = c.getDest();
                break;
              }
              else if (c.getType() == AndersConstraint::GEP) {
                worklist.push(c.getSrc());
                break;
              }
            }
          }
        }
        if (underlyingSrcIndex != AndersNodeFactory::InvalidIndex)
          srcIndex = underlyingSrcIndex;
      }
    }
    constraints.emplace_back(AndersConstraint::GEP, dstIndex, srcIndex, fields);

    //   if (itr != constraints.end()) {
    //     // However, we're more interested as the what the actual object is:
    //     src = nodeFactory.getValueForNode(itr->getSrc());
    //     assert(src != nullptr && "GEP Constraint failed: underlying src is null.");

    //     // We can change our srcIndex, because it may exist already if we've been through this before.
    //     srcIndex = nodeFactory.getObjectNodeFor(src, fields);
    //   }
    // }

    // if (srcIndex == AndersNodeFactory::InvalidIndex) {
    //   // We don't create objects for each field when we encounter an allocation
    //   // ..meaning it's not an oddity if srcIndex is invalid.
    //   srcIndex = nodeFactory.createObjectNode(src, fields);
    // }

    // assert(srcIndex != AndersNodeFactory::InvalidIndex &&
    //        "Failed to find gep src node");
    // NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst, fields);
    // assert(dstIndex != AndersNodeFactory::InvalidIndex &&
    //        "Failed to find gep dst node");

    // constraints.emplace_back(AndersConstraint::ADDR_OF, dstIndex, srcIndex);

    break;
  }
  case Instruction::PHI: {
    if (inst->getType()->isPointerTy()) {
      const PHINode *phiInst = cast<PHINode>(inst);
      NodeIndex dstIndex = nodeFactory.getValueNodeFor(phiInst);
      assert(dstIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find phi dst node");
      for (unsigned i = 0, e = phiInst->getNumIncomingValues(); i != e; ++i) {
        NodeIndex srcIndex =
            nodeFactory.getValueNodeFor(phiInst->getIncomingValue(i));
        assert(srcIndex != AndersNodeFactory::InvalidIndex &&
               "Failed to find phi src node");
        constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
      }
    }
    break;
  }
  case Instruction::BitCast: {
    if (inst->getType()->isPointerTy()) {
      NodeIndex srcIndex = nodeFactory.getValueNodeFor(inst->getOperand(0));
      assert(srcIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find bitcast src node");
      NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
      assert(dstIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find bitcast dst node");
      constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
    }
    break;
  }
  case Instruction::IntToPtr: {
    assert(inst->getType()->isPointerTy());

    // Get the node index for dst
    NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
    assert(dstIndex != AndersNodeFactory::InvalidIndex &&
           "Failed to find inttoptr dst node");

    // We use pattern matching to look for a matching ptrtoint
    Value *op = inst->getOperand(0);

    // Pointer copy: Y = inttoptr (ptrtoint X)
    Value *srcValue = nullptr;
    if (PatternMatch::match(
            op, PatternMatch::m_PtrToInt(PatternMatch::m_Value(srcValue)))) {
      NodeIndex srcIndex = nodeFactory.getValueNodeFor(srcValue);
      assert(srcIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find inttoptr src node");
      NodeIndex freshObj = nodeFactory.createObjectNode(nullptr);
      NodeIndex freshPtr = nodeFactory.createValueNode(nullptr);
      constraints.emplace_back(AndersConstraint::ADDR_OF, freshPtr, freshObj);
      constraints.emplace_back(AndersConstraint::COPY, dstIndex, freshPtr);
      break;
    }

    // Pointer arithmetic: Y = inttoptr (ptrtoint (X) + offset)
    if (PatternMatch::match(
            op, PatternMatch::m_Add(
                    PatternMatch::m_PtrToInt(PatternMatch::m_Value(srcValue)),
                    PatternMatch::m_Value()))) {
      NodeIndex srcIndex = nodeFactory.getValueNodeFor(srcValue);
      assert(srcIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find inttoptr src node");
      constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
      break;
    }

    // Otherwise, we really don't know what dst points to
    constraints.emplace_back(AndersConstraint::COPY, dstIndex,
                             nodeFactory.getUniversalPtrNode());

    break;
  }
  case Instruction::Select: {
    if (inst->getType()->isPointerTy()) {
      NodeIndex srcIndex1 = nodeFactory.getValueNodeFor(inst->getOperand(1));
      assert(srcIndex1 != AndersNodeFactory::InvalidIndex &&
             "Failed to find select src node 1");
      NodeIndex srcIndex2 = nodeFactory.getValueNodeFor(inst->getOperand(2));
      assert(srcIndex2 != AndersNodeFactory::InvalidIndex &&
             "Failed to find select src node 2");
      NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
      assert(dstIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find select dst node");
      constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex1);
      constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex2);
    }
    break;
  }
  case Instruction::VAArg: {
    if (inst->getType()->isPointerTy()) {
      NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
      assert(dstIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find va_arg dst node");
      NodeIndex vaIndex =
          nodeFactory.getVarargNodeFor(inst->getParent()->getParent());
      assert(vaIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find vararg node");
      constraints.emplace_back(AndersConstraint::COPY, dstIndex, vaIndex);
    }
    break;
  }
  case Instruction::ExtractValue: {
      if (!inst->getType()->isPointerTy()) break;
      NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
      assert(dstIndex != AndersNodeFactory::InvalidIndex);

      const ExtractValueInst *evi = cast<ExtractValueInst>(inst);
      const Value *aggOp = evi->getAggregateOperand();

      if (!isa<Constant>(aggOp)) {
          NodeIndex srcIndex = nodeFactory.getValueNodeFor(aggOp);
          if (srcIndex != AndersNodeFactory::InvalidIndex)
              constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
      }
      break;
  }
  case Instruction::InsertValue: {
      if (!typeContainsPointer(inst->getType())) break;
      NodeIndex dstIndex = nodeFactory.getValueNodeFor(inst);
      if (dstIndex == AndersNodeFactory::InvalidIndex) break;

      const InsertValueInst *ivi = cast<InsertValueInst>(inst);
      const Value *insertedVal = ivi->getInsertedValueOperand();
      const Value *aggOp = ivi->getAggregateOperand();
      if (insertedVal->getType()->isPointerTy() && !isa<Constant>(insertedVal)) {
          NodeIndex srcIndex = nodeFactory.getValueNodeFor(insertedVal);
          if (srcIndex != AndersNodeFactory::InvalidIndex)
              constraints.emplace_back(AndersConstraint::COPY, dstIndex, srcIndex);
      }
      if (!isa<Constant>(aggOp) && typeContainsPointer(aggOp->getType())) {
          NodeIndex aggIndex = nodeFactory.getValueNodeFor(aggOp);
          if (aggIndex != AndersNodeFactory::InvalidIndex)
              constraints.emplace_back(AndersConstraint::COPY, dstIndex, aggIndex);
      }
      break;
  }
  // We have no intention to support exception-handling in the near future
  case Instruction::LandingPad:
  case Instruction::Resume:
  // Atomic instructions can be modeled by their non-atomic counterparts. To be
  // supported
  case Instruction::AtomicRMW:
  case Instruction::AtomicCmpXchg: {
    // errs() << *inst << "\n";
    // llvm_unreachable("not implemented yet");
  }
  default: {
    if (inst->getType()->isPointerTy()) {
      // errs() << *inst << "\n";
      // llvm_unreachable("pointer-related inst not handled!");
    }
    break;
  }
  }
}

// There are two types of constraints to add for a function call:
// - ValueNode(callsite) = ReturnNode(call target)
// - ValueNode(formal arg) = ValueNode(actual arg)
void Andersen::addConstraintForCall(const CallBase* cs) {
  // Ignore asm calls.
  if (cs->isInlineAsm()) return;

  if (const Function *f = cs->getCalledFunction()) // Direct call
  {
    if (f->isDeclaration() || f->isIntrinsic()) // External library call
    {
      // Handle libraries separately
      if (addConstraintForExternalLibrary(cs, f))
        return;
      else // Unresolved library call: ruin everything!
      {
        // errs() << "Unresolved ext function: " << f->getName() << "\n";
        if (cs->getFunctionType()->isPointerTy()) {
          NodeIndex retIndex = nodeFactory.getValueNodeFor(cs);
          assert(retIndex != AndersNodeFactory::InvalidIndex &&
                 "Failed to find ret node!");
          NodeIndex fObj = nodeFactory.createObjectNode(cs);
          constraints.emplace_back(AndersConstraint::ADDR_OF, retIndex, fObj);
        }
        // for (CallBase::const_op_iterator itr = cs->arg_begin(),
        //                                      ite = cs->arg_end();
        //      itr != ite; ++itr) {
        //   Value *argVal = *itr;
        //   if (argVal->getType()->isPointerTy()) {
        //     NodeIndex argIndex = nodeFactory.getValueNodeFor(argVal);
        //     assert(argIndex != AndersNodeFactory::InvalidIndex &&
        //            "Failed to find arg node!");
        //     NodeIndex fObj = nodeFactory.createObjectNode(cs);
        //     constraints.emplace_back(AndersConstraint::ADDR_OF, argIndex, fObj);
        //   }
        // }
      }
    } else // Non-external function call
    {
      addReturnConstraintForCall(cs, f);
      addArgumentConstraintForCall(cs, f);
    }
  }
}

void Andersen::addReturnConstraintForCall(const CallBase *cs, const Function *f) {
  const Type *retTy = f->getReturnType();
  if (retTy->isPointerTy() || typeContainsPointer(retTy)) {
      NodeIndex retIndex = nodeFactory.getValueNodeFor(cs);
      if (retIndex != AndersNodeFactory::InvalidIndex) {
          NodeIndex fRetIndex = nodeFactory.getReturnNodeFor(f);
          if (fRetIndex != AndersNodeFactory::InvalidIndex)
              constraints.emplace_back(AndersConstraint::COPY, retIndex, fRetIndex);
      }
  }
}

void Andersen::addArgumentConstraintForCall(const CallBase *cs, const Function *f) {
  Function::const_arg_iterator fItr = f->arg_begin();
  CallBase::User::const_op_iterator aItr = cs->arg_begin();
  while (fItr != f->arg_end() && aItr != cs->arg_end()) {
    const Argument *formal = &*fItr;
    const Value *actual = *aItr;

    if (formal->getType()->isPointerTy()) {
      NodeIndex fIndex = nodeFactory.getValueNodeFor(formal);
      assert(fIndex != AndersNodeFactory::InvalidIndex &&
             "Failed to find formal arg node!");
      if (actual->getType()->isPointerTy()) {
        NodeIndex aIndex = nodeFactory.getValueNodeFor(actual);
        assert(aIndex != AndersNodeFactory::InvalidIndex &&
               "Failed to find actual arg node!");
        constraints.emplace_back(AndersConstraint::COPY, fIndex, aIndex);
      } else
        constraints.emplace_back(AndersConstraint::COPY, fIndex,
                                 nodeFactory.getUniversalPtrNode());
    }

    ++fItr, ++aItr;
  }

  // Copy all pointers passed through the varargs section to the varargs node
  if (f->getFunctionType()->isVarArg()) {
    while (aItr != cs->arg_end()) {
      const Value *actual = *aItr;
      if (actual->getType()->isPointerTy()) {
        NodeIndex aIndex = nodeFactory.getValueNodeFor(actual);
        assert(aIndex != AndersNodeFactory::InvalidIndex &&
               "Failed to find actual arg node!");
        NodeIndex vaIndex = nodeFactory.getVarargNodeFor(f);
        assert(vaIndex != AndersNodeFactory::InvalidIndex &&
               "Failed to find vararg node!");
        constraints.emplace_back(AndersConstraint::COPY, vaIndex, aIndex);
      }

      ++aItr;
    }
  }
}

