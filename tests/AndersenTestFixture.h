#pragma once
#include "include/doctest.h"

#include <algorithm>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "Andersen.h"
#include "NodeFactory.h"
#include "PtsSet.h"
#include "NodeMap.h"

#include <memory>
#include <string>
#include <vector>

using namespace std;
using namespace llvm;

class AndersenTestFixture {
public:
    void parseAssembly(const string &assembly) {
        SMDiagnostic error;
        module = parseAssemblyString(assembly.c_str(), error, _context);

        string message;
        raw_string_ostream os(message);
        error.print("", os);

        if (!module)
            report_fatal_error(os.str().c_str());
        makeAndersen(*module);
    }

    const Function* findFunction(const string &name) {
        return module->getFunction(name);
    }

    const Instruction* findInstruction(const Function *func, const string &name) {
        if (!func) return nullptr;

        for (auto &block : *func) {
            for (auto &instr : block) {
                if (instr.getName() == name)
                    return &instr;
            }
        }
        return nullptr;
    }

    const Instruction* findInstruction(const string &functionName, const string &name) {
        return findInstruction(findFunction(functionName), name);
    }

    const GlobalVariable* findGlobal(const string &name) {
        return module->getGlobalVariable(name);
    }

    void assertPtsToSetEmpty(const Value *v) {
        std::vector<const Value*> set;
        andersen->getPointsToSet(v, set);
        REQUIRE(set.empty());
    }

    void assertPtsToSetSize(const Value *v, size_t size) {
        std::vector<const Value*> set;
        andersen->getPointsToSet(v, set);
        REQUIRE(size == set.size());
    }

    void assertPtsToContains(const Value *p, const Value *q) {
        std::vector<const Value*> set;
        andersen->getPointsToSet(p, set);
        REQUIRE(
            std::find(set.begin(), set.end(), q) != set.end()
        );
    }

    void assertPtsToExact(const Value *p, vector<const Value*> qs) {
        std::vector<const Value*> set;
        andersen->getPointsToSet(p, set);
        REQUIRE((
            set.size() == qs.size() &&
            std::is_permutation(set.begin(), set.end(), qs.begin())
        ));
    }

private:
    void makeAndersen(Module &module) {
        andersen = std::make_unique<Andersen>(module);
    }

private:
    LLVMContext _context;

protected:
    std::unique_ptr<Module> module;
    std::unique_ptr<Andersen> andersen;
};
