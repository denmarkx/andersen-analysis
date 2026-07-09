#pragma once

struct Context;

#include "llvm/ADT/APInt.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Constants.h"
#include "llvm/ADT/DenseMap.h"
using namespace llvm;

#include <ranges>

typedef llvm::SmallVector<unsigned int, 4> FieldType;

template<>
struct DenseMapInfo<FieldType> {
    static inline FieldType getEmptyKey() {
        return FieldType{~0u};
    }

    static inline FieldType getTombstoneKey() {
        return FieldType{~0u - 1};
    }

    static unsigned getHashValue(const FieldType &field) {
        return hash_combine_range(field.begin(), field.end());
    }

    static bool isEqual(const FieldType &lhs, const FieldType &rhs) {
        return lhs == rhs;
    }
};

class NodeMap {
public:
    using NodeIndex = unsigned int;
    using NodeMapType = llvm::DenseMap<std::tuple<const llvm::Value*, FieldType>, NodeIndex>;

    NodeIndex insert(const llvm::Value *val, FieldType fields) {
        unsigned int nodeIndex = size();

        fields = fields.empty() ? getFields(val) : fields;

        _map[{val, fields}] = nodeIndex;
        return nodeIndex;
    }

    NodeIndex get(const llvm::Value *val) const {
        return _map.lookup({val, getFields(val)});
    }

    /*
     * Returns a vector of fields that are tracked by the NodeMap.
    */
    std::vector<FieldType> lookupFields(const llvm::Value *val) const {
        auto matches = _map | std::views::filter([&](const auto &x) {
            return std::get<0>(x.first) == val;
        });
        std::vector<FieldType> fields;
        for (const auto &x : matches) {
            fields.push_back(std::get<1>(x.first));
        }
        return fields;
    }

    /*
     * Returns the associated nodeIdx or InvalidIndex.
    */
    NodeIndex find(const llvm::Value *val, FieldType fields) const {
        if (fields.empty())
            fields = getFields(val);
        auto itr = _map.find({val, fields});
        if (itr == _map.end())
            return InvalidIndex;
        return itr->getSecond();
    }

    bool contains(const llvm::Value *val, FieldType fields) const {
        fields = fields.empty() ? getFields(val) : fields;
        return _map.contains({val, fields});
    }

    NodeIndex& operator[](const llvm::Value * value) {
        return _map[{value, getFields(value)}];
    }

    NodeIndex& operator[](std::tuple<const llvm::Value *, FieldType> value) {
        const llvm::Value *val = std::get<0>(value);
        FieldType fields = std::get<1>(value);

        fields = fields.empty() ? getFields(val) : fields;
        return _map[{val, fields}];
    }

    void erase(const llvm::Value *val) {
        _map.erase({val, getFields(val)});
    }

    const unsigned int size() const { return _map.size(); }
    NodeMapType::const_iterator begin() const { return _map.begin(); }
    NodeMapType::const_iterator end() const { return _map.end(); }

public:
    void setDataLayout(const DataLayout *layout) {
        assert(_layout == nullptr);
        _layout = const_cast<DataLayout*>(layout);
    }

    /*
     * Attempts to resolve the indices that this value uses.
    */
    FieldType getFields(const llvm::Value *value) const {
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
                        // Byte-wise we move sizeof(ptrType)*offsetInt
                        // The main assumption here is that this won't put us
                        // in the middle of the aggregate..non-standard layouts might..
                        llvm::TypeSize size = _layout->getTypeAllocSize(ptrType);
                        APInt ap = APInt(
                            _layout->getIndexTypeSizeInBits(offsetInt->getType()),
                            size * offsetInt->getZExtValue()
                        );

                        auto indices = _layout->getGEPIndicesForOffset(ptrType, ap);
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

private:
    const llvm::Value* findAggregateFromParam(const llvm::Value *param) const;

    bool isAggregateGEP(const llvm::Value *value) const {
        if (const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(value))
            return gep->getSourceElementType()->isAggregateType();
        return false;
    }

    /*
     * Walks the DEF-USE graph to attempt to find the underlying type.
    */
    llvm::Type* findType(const llvm::Value *value) const {
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

    void printFields(FieldType &fields) const {
        errs() << "fields = [";
        for (const auto &v : fields) {
            errs() << v << ", ";
        }
        errs() << "]\n";
    }

private:
    NodeMapType _map;
    DataLayout *_layout = nullptr;

    static constexpr unsigned int InvalidIndex = ~0u;
};
