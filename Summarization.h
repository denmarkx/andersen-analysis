#pragma once

#include "llvm/IR/Instructions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/DenseMap.h"
#include <ostream>
#include <optional>
using namespace llvm;

struct ParameterSummary {
    bool loadsPointer = false;
    bool storesPointer = false;
    bool escapes = false;

    const bool requiresContext() const {
        return loadsPointer || storesPointer || escapes;
    }

    friend std::ostream& operator<<(std::ostream& out, const ParameterSummary &summary);
};

struct ParameterSummaryGroup {
    SmallVector<unsigned, 4> parameterIndices;
    SmallVector<ParameterSummary, 4> parameterSummaries;
};

class Summarization {
public:
    void summarizeParameters(const Function*, unsigned int);

private:
    std::optional<ParameterSummary> summarizeParameter(const Argument&);

private:
    DenseMap<unsigned, ParameterSummaryGroup> _summaries;
};