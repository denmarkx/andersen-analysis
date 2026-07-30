#include "ContextManager.h"

void ContextManager::registerContextObject(NodeIndex objectId) {
    assert(objectId != ~0u && "registerContextObject: objectId is an invalid index.");
    _contextObjects.push_back(objectId);
}

bool ContextManager::isContextObject(NodeIndex objectId) {
    return std::find(_contextObjects.begin(), _contextObjects.end(), objectId) != _contextObjects.end();
}

void ContextManager::registerFunctionContext(NodeIndex baseFunctionIdx, ContextType context,
    NodeIndex functionIdx, llvm::SmallVector<NodeIndex, 4> &parameterIdxs) {
    assert(!_functionContextCache.contains({baseFunctionIdx, context}));

    FunctionContext functionCtx = FunctionContext { functionIdx, parameterIdxs };
    _functionContextCache[{baseFunctionIdx, context}] = functionCtx;
}

bool ContextManager::doesFunctionContextExist(NodeIndex baseFunctionIdx, ContextType context) const {
    return _functionContextCache.contains({baseFunctionIdx, context});
}

const std::optional<FunctionContext> ContextManager::getFunctionContext(NodeIndex baseFunctionIdx, ContextType context) const {
    auto it = _functionContextCache.find({baseFunctionIdx, context});
    if (it == _functionContextCache.end())
        return {};
    return it->second;
}
