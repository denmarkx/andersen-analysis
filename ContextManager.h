#ifndef ANDERSEN_CONTEXTMANAGER_H
#define ANDERSEN_CONTEXTMANAGER_H

#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Value.h>

typedef unsigned int ContextType;
inline unsigned int NoContext = 0;

class ContextManager {
public:
    void registerHeapPointer(unsigned int);
    bool isHeapObject(unsigned int);

private:
    llvm::SmallVector<unsigned int> _heapPointers;
};

#endif