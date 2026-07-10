#pragma once

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

    NodeIndex insert(const llvm::Value *val, FieldType fields);
    NodeIndex get(const llvm::Value *val) const;

    std::vector<FieldType> lookupFields(const llvm::Value *val) const;
    NodeIndex find(const llvm::Value *val, FieldType fields) const;
    bool contains(const llvm::Value *val, FieldType fields) const;
    void erase(const llvm::Value *val);

    NodeIndex& operator[](const llvm::Value * value);
    NodeIndex& operator[](std::tuple<const llvm::Value *, FieldType> value);

    const unsigned int size() const;
    NodeMapType::const_iterator begin() const;
    NodeMapType::const_iterator end() const;

    void setDataLayout(const DataLayout *layout);
    FieldType getFields(const llvm::Value *value) const;

private:
    const llvm::Value* findAggregateFromParam(const llvm::Value *param) const;
    bool isAggregateGEP(const llvm::Value *value) const;
    llvm::Type* findType(const llvm::Value *value) const;
    void printFields(FieldType &fields) const;

private:
    NodeMapType _map;
    DataLayout *_layout = nullptr;

    static constexpr unsigned int InvalidIndex = ~0u;
};
