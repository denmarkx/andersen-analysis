#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "Andersen.h"

using namespace llvm;

int main(int argc, char *argv[]) {
    LLVMContext ctx;
    SMDiagnostic error;

    std::unique_ptr<Module> module = parseIRFile("files/test.ll", error, ctx);

    if (module == nullptr) {
        error.print("", errs());
        return 0;
    }

    legacy::PassManager PM;
    PM.add(new AndersenAAWrapperPass());
    PM.run(*module);
    return 0;
}
