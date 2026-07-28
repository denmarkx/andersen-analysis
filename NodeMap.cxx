#include "NodeMap.h"

#include "ContextManager.h"
#include "NodeMapUtil.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/IR/Instructions.h"

void NodeMap::insert(const llvm::Value *val, const ContextType context, FieldType fields, NodeIndex idx) {
    _map[hash(val, context, fields)] = idx;
}

NodeMap::NodeIndex NodeMap::get(const llvm::Value *val, const ContextType context, FieldType fields) const {
    if (!contains(val, context, fields))
        return InvalidIndex;
    return _map.lookup(hash(val, context, fields));
}

bool NodeMap::contains(const llvm::Value *val, const ContextType context, FieldType fields) const {
    return _map.contains(hash(val, context, fields));
}

void NodeMap::erase(const llvm::Value *val, const ContextType context) {
    _map.erase(hash(val, context, {}));
}

const unsigned int NodeMap::size() const {
    return _map.size();
}

NodeMap::NodeMapType::const_iterator NodeMap::begin() const {
    return _map.begin();

}

NodeMap::NodeMapType::const_iterator NodeMap::end() const {
    return _map.end();
}

uint64_t NodeMap::hash(const Value *v, const ContextType context, FieldType fields) const {
    fields = fields.empty() ? NodeMapUtil::getFields(v) : fields;
    return hash_combine(context,
        hash_combine(v, hash_combine_range(fields.begin(), fields.end()))
    );
}
