#pragma once

#include "ContextManager.h"
#include "llvm/IR/Instructions.h"
#include "llvm/ADT/DenseMap.h"
using namespace llvm;

typedef llvm::SmallVector<unsigned int, 4> FieldType;

class NodeMap {
public:
    using NodeIndex = unsigned int;
    using NodeMapType = DenseMap<uint64_t, NodeIndex>;

public:
    void insert(const llvm::Value *, ContextType context = NoContext, FieldType = {}, NodeIndex = ~0u);
    NodeIndex get(const llvm::Value *, ContextType context = NoContext, FieldType = {}) const;
    bool contains(const llvm::Value *, ContextType context = NoContext, FieldType = {}) const;
    void erase(const llvm::Value *, ContextType context = NoContext);

    const unsigned int size() const;
    NodeMapType::const_iterator begin() const;
    NodeMapType::const_iterator end() const;

private:
    static constexpr unsigned int InvalidIndex = ~0u;
    uint64_t hash(const llvm::Value*, ContextType, FieldType) const;

    NodeMapType _map;
};
