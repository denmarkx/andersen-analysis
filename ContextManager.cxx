#include "ContextManager.h"

void ContextManager::registerHeapPointer(unsigned int objectId) {
    assert(objectId != ~0u && "registerHeapPointer: objectId is an invalid index.");
    _heapPointers.push_back(objectId);
}

bool ContextManager::isHeapObject(unsigned int objectId) {
    return std::find(_heapPointers.begin(), _heapPointers.end(), objectId) != _heapPointers.end();
}
