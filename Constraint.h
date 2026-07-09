#ifndef ANDERSEN_CONSTRAINT_H
#define ANDERSEN_CONSTRAINT_H

#include "NodeFactory.h"
#include "NodeMap.h"

#include <cassert>

/// AndersConstraint - Objects of this structure are used to represent the
/// various constraints identified by the algorithm.  The constraints are
/// 'copy', for statements like "A = B", 'load' for statements like "A = *B",
/// 'store' for statements like "*A = B", and AddressOf for statements like A =
/// alloca;  The Offset is applied as *(A + K) = B for stores, A = *(B + K) for
/// loads, and A = B + K for copies.  It is illegal on addressof constraints
/// (because it is statically resolvable to A = &C where C = B + K)
class AndersConstraint {
public:
  enum ConstraintType {
    ADDR_OF,
    COPY,
    LOAD,
    STORE,
    GEP,
  };

private:
  ConstraintType type;
  NodeIndex dest;
  NodeIndex src;
  FieldType fields;

public:
  AndersConstraint(ConstraintType Ty, NodeIndex D, NodeIndex S, FieldType F={})
      : type(Ty), dest(D), src(S), fields(F) {}

  ConstraintType getType() const { return type; }
  NodeIndex getDest() const { return dest; }
  NodeIndex getSrc() const { return src; }
  FieldType getFields() const { return fields; }

  bool operator==(const AndersConstraint &RHS) const {
    return RHS.type == type && RHS.dest == dest && RHS.src == src;
  }

  bool operator!=(const AndersConstraint &RHS) const { return !(*this == RHS); }

  bool operator<(const AndersConstraint &RHS) const {
    if (RHS.type != type)
      return RHS.type < type;
    else if (RHS.dest != dest)
      return RHS.dest < dest;
    return RHS.src < src;
  }
};

#endif
