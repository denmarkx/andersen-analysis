#ifndef ANDERSEN_CONTEXTMANAGER_H
#define ANDERSEN_CONTEXTMANAGER_H

#include "llvm/ADT/Hashing.h"
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/MapVector.h>
#include <optional>
#include <limits>

typedef unsigned int NodeIndex;
typedef llvm::SmallVector<unsigned int, 4> ContextType;
inline llvm::SmallVector<unsigned int, 4> NoContext = {};

namespace llvm {
    inline hash_code hash_value(const llvm::SmallVector<unsigned int, 4> &vec) {
        return hash_combine_range(vec.begin(), vec.end());
    }

    template<>
    struct DenseMapInfo<ContextType> {
        static inline ContextType getEmptyKey() {
            return ContextType{std::numeric_limits<unsigned int>::max()};
        }

        static inline ContextType getTombstoneKey() {
            return ContextType{std::numeric_limits<unsigned int>::max() - 1};
        }

        static unsigned getHashValue(const ContextType &value) {
            return hash_value(value);
        }

        static bool isEqual(const ContextType &lhs, const ContextType &rhs) {
            return lhs == rhs;
        }
    };
}

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
    llvm::MapVector<std::pair<NodeIndex, ContextType>, FunctionContext> _functionContextCache;
};

#endif