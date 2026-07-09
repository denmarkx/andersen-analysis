#pragma once

#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "Andersen.h"
#include "NodeFactory.h"
#include "PtsSet.h"
#include "NodeMap.h"

#include <memory>
#include <string>

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

    const Function* getFunction(const string &name) {
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
        return findInstruction(getFunction(functionName), name);
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
