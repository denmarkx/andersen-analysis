#ifndef ANDERSEN_PTSSET_H
#define ANDERSEN_PTSSET_H

#include "llvm/ADT/SparseBitVector.h"

// We move the points-to set representation here into a separate class
// The intention is to let us try out different internal implementation of this
// data-structure (e.g. vectors/bitvecs/sets, ref-counted/non-refcounted) easily
class AndersPtsSet {
private:

  llvm::SparseBitVector<> bitvec;

public:
  using iterator = llvm::SparseBitVector<>::iterator;

  // Return true if *this has idx as an element
  // This function should be marked const, but we cannot do it because
  // SparseBitVector::test() is not marked const. WHY???
  bool has(unsigned idx) { return bitvec.test(idx); }
  bool has(unsigned idx) const {
    // Since llvm::SparseBitVector::test() does not have a const quantifier, we
    // have to use this ugly workaround to implement has()
    llvm::SparseBitVector<> idVec;
    idVec.set(idx);
    return bitvec.contains(idVec);
  }

  // Return true if the ptsset changes
  bool insert(unsigned idx) { return bitvec.test_and_set(idx); }

  // Return true if *this is a superset of other
  bool contains(const AndersPtsSet &other) const {
    return bitvec.contains(other.bitvec);
  }

  // intersectWith: return true if *this and other share points-to elements
  bool intersectWith(const AndersPtsSet &other) const {
    return bitvec.intersects(other.bitvec);
  }

  /*
   * Returns boolean indicating if the set contains only the given node.
  */
  bool isSetContainingOnly(unsigned int i) const {
    return (getSize() == 1) && (*begin() == i);
  }

  /*
   * Returns boolean indicating if two sets contain the same values
   * EXCEPT for nodes 0-3.
  */
  bool compareExclude(const AndersPtsSet& other) const {
    if (getSize() != other.getSize()) return false;

    bool same = true;
    for (auto const &idx : *this) {
      if (idx <= 3) continue;
      if (!other.has(idx)) {
        same = false;
        break;
      }
    }
    return same;
  }

  /*
   * Returns boolean indicating if two sets contain at least one overlapping value
   * EXCEPT for nodes 0-3.
  */
  bool compareIntersectionExclude(const AndersPtsSet& other) const {
    for (auto const &idx : *this) {
      if (idx <= 3)
        continue;
      if (other.has(idx))
        return true;
    }
    return false;
  }

  // Return true if the ptsset changes
  bool unionWith(const AndersPtsSet &other) { return bitvec |= other.bitvec; }

  bool subtract(const AndersPtsSet &other, AndersPtsSet &out) const {
    out = *this;
    out.bitvec.intersectWithComplement(other.bitvec);
    return !out.isEmpty();
  }

  void clear() { bitvec.clear(); }

  unsigned getSize() const {
    return bitvec.count(); // NOT a constant time operation!
  }
  bool
  isEmpty() const // Always prefer using this function to perform empty test
  {
    return bitvec.empty();
  }

  bool operator==(const AndersPtsSet &other) const {
    return bitvec == other.bitvec;
  }

  iterator begin() const { return bitvec.begin(); }
  iterator end() const { return bitvec.end(); }
};

#endif
