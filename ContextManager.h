#ifndef ANDERSEN_CONTEXTMANAGER_H
#define ANDERSEN_CONTEXTMANAGER_H

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/DenseMap.h>
#include <optional>

typedef unsigned int NodeIndex;
typedef unsigned int ContextType;
inline unsigned int NoContext = 0;

struct FunctionContext {
    NodeIndex functionIdx;
    llvm::SmallVector<NodeIndex, 4> parameterIdxs;
};

class ContextManager {
public:
    void registerContextObject(NodeIndex);
    bool isContextObject(NodeIndex);

    void registerFunctionContext(NodeIndex, ContextType, NodeIndex, llvm::SmallVector<NodeIndex, 4>&);
    bool doesFunctionContextExist(NodeIndex, ContextType) const;
    const std::optional<FunctionContext> getFunctionContext(NodeIndex, ContextType) const;

private:
    llvm::SmallVector<NodeIndex, 8> _contextObjects;

    // _functionContextCache is {{generalFunctionIdx, objIdx}, ContextFunction}
    //  where generalFunctionIdx is just the NodeIndex for the function where context = NoContext.
    llvm::DenseMap<std::pair<NodeIndex, ContextType>, FunctionContext> _functionContextCache;
};

#endif