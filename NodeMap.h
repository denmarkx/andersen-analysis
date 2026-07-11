#pragma once

#include "llvm/IR/Instructions.h"
#include "llvm/ADT/DenseMap.h"
using namespace llvm;

typedef llvm::SmallVector<unsigned int, 4> FieldType;

class NodeMap {
public:
    using NodeIndex = unsigned int;
    using NodeMapType = DenseMap<uint64_t, NodeIndex>;

public:
    void insert(const llvm::Value *, FieldType, NodeIndex);
    NodeIndex get(const llvm::Value *, FieldType) const;
    bool contains(const llvm::Value *, FieldType) const;
    void erase(const llvm::Value *);

    const unsigned int size() const;
    NodeMapType::const_iterator begin() const;
    NodeMapType::const_iterator end() const;

private:
    static constexpr unsigned int InvalidIndex = ~0u;
    uint64_t hash(const llvm::Value*, FieldType) const;

    NodeMapType _map;
};
