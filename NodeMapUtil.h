#pragma once

#include "llvm/IR/Operator.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include <queue>
using namespace llvm;

namespace NodeMapUtil {
    using FieldType = llvm::SmallVector<unsigned int, 4>;

    /*
     * Returns boolean indicating if the source element type is an aggregate.
    */
    inline bool isAggregateGEP(const llvm::Value *value) {
        if (const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(value))
            return gep->getSourceElementType()->isAggregateType();
        return false;
    }

    /*
     * Given a parameters, attempts to find an instruction within its users that
     * provides information regarding the pointer type and if it's an aggregate.
     * More specifically, it attempts to identify a GEP instruction.
    */
    inline const llvm::Value* findAggregateFromParam(const llvm::Value *param) {
        if (!param->getType()->isPointerTy()) return nullptr;

        // If this is a gep already..or allocas, we return it.
        if (isAggregateGEP(param) || isa<AllocaInst>(param))
            return param;

        // Load instructions are particularly useful:
        if (isa<LoadInst>(param)) {
            // This is only hit when previously itr = users().end.
            // ..so the user, in this case, is the 1st operand.
            return findAggregateFromParam(dyn_cast<LoadInst>(param)->getOperand(0));
        }

        auto itr = std::find_if(param->users().begin(), param->users().end(), [&](const User *user) {
            // I don't think it's important to handle every such case,
            // but if it's a GEP instruction, then that's a smoking gun.
            return
                (
                    isAggregateGEP(user) ||
                    isa<LoadInst>(user)
                );
        });

        if (itr == param->users().end()) {
            // Let's move up to the original callsite and check the arg's uses.
            // I suppose only if we're still a formal argument:
            const Argument *formal = dyn_cast<Argument>(param);
            if (!formal) return nullptr;

            if (1) {
                // const Function *function = formal->getParent();

                // // Get the index of this param.
                // unsigned int paramId = ~0u;
                // for (unsigned int i=0; i < function->arg_size(); i++)
                //     if (function->getArg(i) == formal)
                //         paramId = i;

                // if (paramId == ~0u)
                //     return nullptr;

                // const CallBase *call = dyn_cast<CallBase>(ctx->callSite);

                // // Then get the argument from the call:
                // return findAggregateFromParam(call->getArgOperand(paramId));
            }
            return nullptr;
        }

        return *itr;
    }

    /*
     * Walks the DEF-USE graph to attempt to find the underlying type.
    */
    inline llvm::Type* findType(const llvm::Value *value) {
        if (!value) return nullptr;

        // Best case is that this is directly an alloca.
        if (const AllocaInst *alloca = dyn_cast<AllocaInst>(value))
            return alloca->getAllocatedType();

        // Alternatively, a GEP will be useful here:
        if (const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(value)) {
            // Since we know that the GEP is the value,
            // we can assume the source type is OK if the pointer offset is 0.
            const ConstantInt *offsetInt = dyn_cast<ConstantInt>(gep->getOperand(1));
            if (offsetInt && offsetInt->getZExtValue() == 0)
                return gep->getSourceElementType();
        }

        for (const llvm::User *user : value->users()) {
            llvm::Type *type = findType(user);
            if (type) return type;
        }

        return nullptr;
    }

    /*
     * Attempts to resolve the indices that this value uses.
    */
    inline FieldType getFields(const llvm::Value *value) {
        if (!value) return {};

        FieldType fields;

        // If we're an alloca, fields = {}.
        if (isa<AllocaInst>(value))
            return {};

        // Global Variable:
        if (const GlobalVariable *global = dyn_cast<GlobalVariable>(value)) {
            if (!global->hasInitializer()) return {};

            const Constant *initializer = global->getInitializer();

            // for an aggregate x, &x = &x[0]
            if (initializer->getType()->isAggregateType())
                return {0};
        }

        // Const Expression:
        else if (const ConstantExpr *cExpr = dyn_cast<ConstantExpr>(value))
            return getFields(cExpr->getAsInstruction());

        // GEP:
        else if (const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(value)) {
            fields.reserve(gep->getNumIndices());

            // Pointer offset:
            const llvm::Value *offset = gep->getOperand(1);
            if (const ConstantInt *offsetInt = dyn_cast<ConstantInt>(offset)) {
                if (offsetInt->getZExtValue() > 0) {
                    // Theoretically, we should be able to resolve getFields() on the source.
                    auto indices = getFields(gep->getOperand(0));
                    fields.append(indices.begin(), indices.end());

                    // When the pointer offset is > 0, we need to at least make an attempt
                    // to normalize this back into an index-only instruction.
                    // The main problem with this comes from the fact that we need to figure out
                    // the type of the pointer operand (op0)..because it's an opaque ptr.
                    llvm::Type *ptrType = findType(gep->getOperand(0));

                    // Some instructions will do stuff like use i8 for traversing bytes
                    if (ptrType && ptrType->isAggregateType()) {
                        const DataLayout &layout = gep->getModule()->getDataLayout();

                        // Byte-wise we move sizeof(ptrType)*offsetInt
                        // The main assumption here is that this won't put us
                        // in the middle of the aggregate..non-standard layouts might..
                        llvm::TypeSize size = layout.getTypeAllocSize(ptrType);
                        APInt ap = APInt(
                            layout.getIndexTypeSizeInBits(offsetInt->getType()),
                            size * offsetInt->getZExtValue()
                        );

                        auto indices = layout.getGEPIndicesForOffset(ptrType, ap);
                        for (const auto &e: indices)
                            fields.push_back(e.getZExtValue());
                    }
                }
            }

            for (unsigned int i=2; i < gep->getNumOperands(); i++) {
                if (const ConstantInt *index = dyn_cast<ConstantInt>(gep->getOperand(i))) {
                    fields.push_back(index->getZExtValue());
                    continue;
                }

                // Another thing here is that this may be an actual SSA value.
                // ..in which case we can't reliably know the value.
                // ..TODO: I don't actually know what to do here.
            }
        }

        // Parameter:
        else if (const Argument *param = dyn_cast<Argument>(value)) {
            // TODO: sret will show the aggregate type (param->getParamStructRetType)
            // TODO: DWARF metadata will send me in circles, but that is what should be checked next.

            // The last actual thing I can think of to try is walking the users to find a GEP:
            const llvm::Value *candidate = findAggregateFromParam(param);
            return getFields(candidate);
        }

        return fields;
    }

    /*
     * Returns the number of elements within the current type.
     * Note: This is non-recursive and only returns the top layer.
    */
    inline unsigned int getNumElements(const Type *type) {
        if (type->isStructTy())
            return type->getStructNumElements();
        return type->getArrayNumElements();
    }

    /*
     * Returns current element type given the index.
     * Note: The parameter <i> is unused for arrays.
    */
    inline const Type* getElementType(const Type *type, unsigned int i) {
        if (type->isStructTy())
            return type->getStructElementType(i);
        return type->getArrayElementType();
    };

    /*
     * Returns boolean if the first-class aggregate type (IE: struct or array) contains a pointer.
    */
    inline bool aggregateContainsPointer(const Type* type) {
        if (type->isPointerTy()) return true;
        if (type->isArrayTy())
            return aggregateContainsPointer(type->getArrayElementType());
        if (type->isStructTy()) {
            for (unsigned i = 0; i < type->getStructNumElements(); ++i)
                if (aggregateContainsPointer(type->getStructElementType(i))) return true;
        }
        return false;
    }

    inline void populateAggregateFields(const Type* type, const FieldType &fields, SmallVector<FieldType, 4>& allFields) {
        if (!type->isAggregateType()) return;

        for (unsigned int i=0; i < getNumElements(type); i++) {
            FieldType currentFields;
            currentFields.append(fields);
            currentFields.push_back(i);

            const Type *elementType = getElementType(type, i);

            if (elementType->isAggregateType()) {
                populateAggregateFields(elementType, currentFields, allFields);
                continue;
            }

            allFields.push_back(currentFields);
        }
    }

    /*
     * Returns vector of field paths for each pointer in a first-class aggregate.
    */
    inline SmallVector<FieldType, 4> getAggregateFields(const Type* type) {
        if (!type->isAggregateType()) return {};

        SmallVector<FieldType, 4> allFields;

        for (unsigned int i=0; i < getNumElements(type); i++) {
            const Type *innerType = getElementType(type, i);

            FieldType fields;
            fields.push_back(i);

            // If the inner is not an agg type, this goes directly in allFields:
            if (!innerType->isAggregateType()) {
                allFields.push_back(fields);
                continue;
            }

            populateAggregateFields(innerType, fields, allFields);
        }

        return allFields;
    }
};
