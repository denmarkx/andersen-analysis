#include "NodeMap.h"
#include "NodeMapUtil.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/IR/Instructions.h"

void NodeMap::insert(const llvm::Value *val, FieldType fields, NodeIndex idx) {
    _map[hash(val, fields)] = idx;
}

NodeMap::NodeIndex NodeMap::get(const llvm::Value *val, FieldType fields) const {
    if (!contains(val, fields))
        return InvalidIndex;
    return _map.lookup(hash(val, fields));
}

bool NodeMap::contains(const llvm::Value *val, FieldType fields) const {
    return _map.contains(hash(val, fields));
}

void NodeMap::erase(const llvm::Value *val) {
    _map.erase(hash(val, {}));
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

uint64_t NodeMap::hash(const Value *v, FieldType fields) const {
    fields = fields.empty() ? NodeMapUtil::getFields(v) : fields;
    return hash_combine(v, hash_combine_range(fields.begin(), fields.end()));
}
