#include "Andersen.h"
#include "Constraint.h"
#include "NodeFactory.h"
#include "NodeMapUtil.h"

#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <iterator>

using namespace llvm;

static const char *noopFuncs[] = {
    "log", "log10", "exp", "exp2", "exp10", "strcmp", "strncmp", "strlen",
    "atoi", "atof", "atol", "atoll", "remove", "unlink", "rename", "memcmp",
    "free", "execl", "execlp", "execle", "execv", "execvp", "chmod", "puts",
    "write", "open", "create", "truncate", "chdir", "mkdir", "rmdir", "read",
    "pipe", "wait", "time", "stat", "fstat", "lstat", "strtod", "strtof",
    "strtold", "fopen", "fdopen", "fflush", "feof", "fileno", "clearerr",
    "rewind", "ftell", "ferror", "fgetc", "fgetc", "_IO_getc", "fwrite",
    "fread", "fgets", "ungetc", "fputc", "fputs", "putc", "ftell", "rewind",
    "_IO_putc", "fseek", "fgetpos", "fsetpos", "printf", "fprintf", "sprintf",
    "vprintf", "vfprintf", "vsprintf", "scanf", "fscanf", "sscanf",
    "__assert_fail", "modf", "putchar", "isalnum", "isalpha", "isascii",
    "isatty", "isblank", "iscntrl", "isdigit", "isgraph", "islower", "isprint",
    "ispunct", "isspace", "isupper", "iswalnum", "iswalpha", "iswctype",
    "iswdigit", "iswlower", "iswspace", "iswprint", "iswupper", "sin", "cos",
    "sinf", "cosf", "asin", "acos", "tan", "atan", "fabs", "pow", "floor",
    "ceil", "sqrt", "sqrtf", "hypot", "random", "tolower", "toupper",
    "towlower", "towupper", "system", "clock", "exit", "abort", "gettimeofday",
    "settimeofday", "sleep", "ctime", "strspn", "strcspn", "localtime",
    "strftime", "qsort", "popen", "pclose", "rand", "rand_r", "srand", "seed48",
    "drand48", "lrand48", "srand48", "__isoc99_sscanf", "__isoc99_fscanf",
    "fclose", "close", "perror",
    "strerror", // this function returns an extenal static pointer
    "__errno_location", "__ctype_b_loc", "abs", "difftime", "setbuf", "_ZdlPv",
    "_ZdaPv", // delete and delete[]
    "fesetround", "fegetround", "fetestexcept", "feraiseexcept",
    "feclearexcept", "llvm.bswap.i16", "llvm.bswap.i32", "llvm.ctlz.i64",
    "llvm.lifetime.start", "llvm.lifetime.end", "llvm.stackrestore", "memset",
    "llvm.memset.i32", "llvm.memset.p0i8.i32", "llvm.memset.i64",
    "llvm.memset.p0i8.i64", "llvm.va_end",
    // The following functions might not be NOOP. They need to be removed from
    // this list in the future
    "setrlimit", "getrlimit", nullptr};

static const char *mallocFuncs[] = {"malloc",
                                    "valloc",
                                    "calloc",
                                    "_Znwj",
                                    "_ZnwjRKSt9nothrow_t",
                                    "_Znwm",
                                    "_ZnwmRKSt9nothrow_t",
                                    "_Znaj",
                                    "_ZnajRKSt9nothrow_t",
                                    "_Znam",
                                    "_ZnamRKSt9nothrow_t",
                                    "strdup",
                                    "strndup",
                                    "getenv",
                                    "memalign",
                                    "posix_memalign",
                                    nullptr};

static const char *reallocFuncs[] = {"realloc", "strtok", "strtok_r", nullptr};

static const char *retArg0Funcs[] = {
    "fgets",    "gets",       "stpcpy",  "strcat",  "strchr",
    "strcpy",   "strerror_r", "strncat", "strncpy", "strpbrk",
    "strptime", "strrchr",    "strstr",  "getcwd",  nullptr};

static const char *retArg1Funcs[] = {
    // Actually the return value of signal() will NOT alias its second argument,
    // but if you call it twice the return values may alias. We're making
    // conservative assumption here
    "signal", nullptr};

static const char *retArg2Funcs[] = {"freopen", nullptr};

static const char *memcpyFuncs[] = {"llvm.memcpy.i32",
                                    "llvm.memcpy.p0i8.p0i8.i32",
                                    "llvm.memcpy.i64",
                                    "llvm.memcpy.p0i8.p0i8.i64",
                                    "llvm.memcpy.p0.p0.i64",
                                    "llvm.memmove.i32",
                                    "llvm.memmove.p0i8.p0i8.i32",
                                    "llvm.memmove.i64",
                                    "llvm.memmove.p0i8.p0i8.i64",
                                    "memccpy",
                                    "memmove",
                                    "bcopy",
                                    nullptr};

static const char *convertFuncs[] = {"strtod",  "strtof",  "strtol", "strtold",
                                     "strtoll", "strtoul", nullptr};

static bool lookupName(const char *table[], const char *str) {
  for (unsigned i = 0; table[i] != nullptr; ++i) {
    if (strcmp(table[i], str) == 0)
      return true;
  }
  return false;
}

const Function* Andersen::lookupCanonicalCalleeFunction(const CallBase *cs) {
  // As of right now, pthread_create moves to the routine function:
  const Function *callee = cs->getCalledFunction();
  if (!callee) return nullptr;

  if (callee->getName() == "pthread_create") {
    Function *routine = dyn_cast<Function>(cs->getArgOperand(2));
    return routine;
  }
  return callee;
}

static unsigned int getAggregateNumElements(const Type *type) {
  if (type->isStructTy())
    return type->getStructNumElements();
  return type->getArrayNumElements();
}

// This function identifies if the external callsite is a library function call,
// and add constraint correspondingly If this is a call to a "known" function,
// add the constraints and return true. If this is a call to an unknown
// function, return false.
bool Andersen::addConstraintForExternalLibrary(const CallBase *cs, const Function *f, const ContextType context, const ContextType funcCtxId) {
  assert(f != nullptr && "called function is nullptr!");
  assert((f->isDeclaration() || f->isIntrinsic()) &&
         "Not an external function!");

  // These functions don't induce any points-to constraints
  if (lookupName(noopFuncs, f->getName().data()))
    return true;

  // Realloc-like library is a little different: if the first argument is
  // nullptr, then it behaves like retArg0Funcs; otherwise, it behaves like
  // mallocFuncs
  bool isReallocLike = lookupName(reallocFuncs, f->getName().data());

  // Library calls that might allocate memory.
  if (lookupName(mallocFuncs, f->getName().data()) ||
      (isReallocLike && !isa<ConstantPointerNull>(cs->getArgOperand(0)))) {
    const Instruction *inst = cs;
    // Create the obj node
    NodeIndex objIndex = nodeFactory.createObjectNode(inst, context);

    // Get the pointer node
    NodeIndex ptrIndex = nodeFactory.getValueNodeFor(inst, context);
    if (ptrIndex == AndersNodeFactory::InvalidIndex) {
      // Must be something like posix_memalign()
      if (f->getName() == "posix_memalign") {
        ptrIndex = nodeFactory.getValueNodeFor(cs->getArgOperand(0), context);
        assert(ptrIndex != AndersNodeFactory::InvalidIndex &&
               "Failed to find arg0 node");
        NodeIndex fPtr = nodeFactory.createValueNode(nullptr, context);
        constraints.emplace_back(AndersConstraint::STORE, fPtr, objIndex);
        constraints.emplace_back(AndersConstraint::STORE, ptrIndex, fPtr);
      } else {
        errs() << f->getName() << '\n';
        assert(false && "unrecognized malloc call");
      }
    } else {
      // Normal malloc-like call
      constraints.emplace_back(AndersConstraint::ADDR_OF, ptrIndex, objIndex);
    }

    return true;
  }

  // sometimes these are annotated by allockind("alloc").
  unsigned int allocAttr = static_cast<unsigned int>(f->getAttributes().getAllocKind());
  if (allocAttr & (unsigned int) AllocFnKind::Alloc) {
    const Instruction *inst = cs;
    NodeIndex objIndex = nodeFactory.createObjectNode(inst, context);
    NodeIndex ptrIndex = nodeFactory.getValueNodeFor(inst, context);
    constraints.emplace_back(AndersConstraint::ADDR_OF, ptrIndex, objIndex);
    _contextMgr.registerContextObject(objIndex);
    return true;
  }

  if (lookupName(retArg0Funcs, f->getName().data()) ||
      (isReallocLike && isa<ConstantPointerNull>(cs->getArgOperand(0)))) {
    NodeIndex retIndex = nodeFactory.getValueNodeFor(cs, context);
    if (retIndex != AndersNodeFactory::InvalidIndex) {
      NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs->getArgOperand(0), context);
      assert(arg0Index != AndersNodeFactory::InvalidIndex &&
             "Failed to find arg0 node");
      constraints.emplace_back(AndersConstraint::COPY, retIndex, arg0Index);
    }

    return true;
  }

  if (lookupName(retArg1Funcs, f->getName().data())) {
    return false; // TODO; signal()
    NodeIndex retIndex = nodeFactory.getValueNodeFor(cs);
    assert(retIndex != AndersNodeFactory::InvalidIndex &&
           "Failed to find call site node");
    NodeIndex arg1Index = nodeFactory.getValueNodeFor(cs->getArgOperand(1));
    assert(arg1Index != AndersNodeFactory::InvalidIndex &&
           "Failed to find arg1 node");
    constraints.emplace_back(AndersConstraint::COPY, retIndex, arg1Index);
    return true;
  }

  if (lookupName(retArg2Funcs, f->getName().data())) {
    NodeIndex retIndex = nodeFactory.getValueNodeFor(cs, context);
    assert(retIndex != AndersNodeFactory::InvalidIndex &&
           "Failed to find call site node");
    NodeIndex arg2Index = nodeFactory.getValueNodeFor(cs->getArgOperand(2), context);
    assert(arg2Index != AndersNodeFactory::InvalidIndex &&
           "Failed to find arg2 node");
    constraints.emplace_back(AndersConstraint::COPY, retIndex, arg2Index);
    return true;
  }

  if (lookupName(memcpyFuncs, f->getName().data())) {
    NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs->getArgOperand(0), context);
    assert(arg0Index != AndersNodeFactory::InvalidIndex &&
           "Failed to find arg0 node");
    NodeIndex arg1Index = nodeFactory.getValueNodeFor(cs->getArgOperand(1), context);
    assert(arg1Index != AndersNodeFactory::InvalidIndex &&
           "Failed to find arg1 node");

    const llvm::Value *src = cs->getArgOperand(1);
    const llvm::Value *dest = cs->getArgOperand(0);
    const llvm::ConstantInt *bytes = dyn_cast<llvm::ConstantInt>(cs->getArgOperand(2));
    
    llvm::Type *srcType = NodeMapUtil::findType(src);
    llvm::Type *dstType = NodeMapUtil::findType(dest);
    const DataLayout &layout = cs->getModule()->getDataLayout();

    bool useCopyConstraint = true;
    if (srcType && bytes) {
      uint64_t size = layout.getTypeAllocSize(srcType).getFixedValue();

      // if the sizeof(srcType) and operand 2 are the same, we just do a regular constraint.
      if ((srcType->isAggregateType() || (dstType && dstType->isAggregateType())) &&
           (getAggregateNumElements(srcType) > 1 || (dstType && getAggregateNumElements(dstType) > 1))) {
        APInt offset = APInt(layout.getTypeAllocSize(srcType), bytes->getZExtValue());

        auto allIndices = NodeMapUtil::recursiveGetIndicesBelowOffset(srcType, offset.getZExtValue(), layout);
        useCopyConstraint = false;

        for (const auto &fullIndices : allIndices) {
          auto indices = fullIndices;

          // A few optimizations (which can probably be moved to the NMU func) so that we don't
          // end up creating more values and constraints than what is needed:
          // TODO: this is useful, but its inconsistent with how geps are currently done.

          // case of: &struct = &struct[0]
          // if (indices.size() == 1 && indices[0] == 0) continue;

          // same as above, just for trailing indices.
          // auto it = indices.end();
          // int trimRight = 0;
          // while (it != indices.begin()) {
            // --it;
            // if (*it == 0)
              // trimRight++;
          // }

          // indices.pop_back_n(trimRight);
          // if (indices.empty()) continue;

          // same as above, just for inner indices.
          // int sum = 0;
          // for (const auto &i : indices) sum += i;
          // if (sum == 0) continue;

          // we simulate a GEP constraint flow here:
          // NodeIndex srcGEPIndex = nodeFactory.createValueNode(nullptr, context);
          NodeIndex srcTmpIndex = nodeFactory.createValueNode(nullptr, context);
          // NodeIndex dstGEPIndex = nodeFactory.createValueNode(nullptr, context);

          // constraints.emplace_back(AndersConstraint::GEP, srcGEPIndex, arg1Index, indices); // &src[indices]

          // dstGEPIndex doesn't blindly follow indices, it accumlates it..similar to what ConstraintCollect does.
          auto dstIndices = NodeMapUtil::getFields(dest);
          dstIndices.insert(dstIndices.end(), indices.begin(), indices.end());

          // constraints.emplace_back(AndersConstraint::GEP, dstGEPIndex, arg0Index, dstIndices); // &dst[dstIndices]
          constraints.emplace_back(AndersConstraint::LOAD, srcTmpIndex, arg1Index, indices); // srcTmpIndex = *src[indices]
          constraints.emplace_back(AndersConstraint::STORE, arg0Index, srcTmpIndex, dstIndices); // *srcTmpIndex = &dst[indices]
        }
      }
    }

    if (useCopyConstraint)
      constraints.emplace_back(AndersConstraint::COPY, arg0Index, arg1Index);

    // Don't forget the return value
    NodeIndex retIndex = nodeFactory.getValueNodeFor(cs, context);
    if (retIndex != AndersNodeFactory::InvalidIndex)
      constraints.emplace_back(AndersConstraint::COPY, retIndex, arg0Index);

    return true;
  }

  if (lookupName(convertFuncs, f->getName().data())) {
    if (!isa<ConstantPointerNull>(cs->getArgOperand(1))) {
      NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs->getArgOperand(0), context);
      assert(arg0Index != AndersNodeFactory::InvalidIndex &&
             "Failed to find arg0 node");
      NodeIndex arg1Index = nodeFactory.getValueNodeFor(cs->getArgOperand(1), context);
      assert(arg1Index != AndersNodeFactory::InvalidIndex &&
             "Failed to find arg1 node");
      constraints.emplace_back(AndersConstraint::STORE, arg0Index, arg1Index);
    }

    return true;
  }

  if (f->getName() == "llvm.va_start") {
    const Instruction *inst = cs;
    const Function *parentF = inst->getParent()->getParent();
    assert(parentF->getFunctionType()->isVarArg());
    NodeIndex arg0Index = nodeFactory.getValueNodeFor(cs->getArgOperand(0), context);
    assert(arg0Index != AndersNodeFactory::InvalidIndex &&
           "Failed to find arg0 node");
    NodeIndex vaIndex = nodeFactory.getVarargNodeFor(parentF);
    assert(vaIndex != AndersNodeFactory::InvalidIndex &&
           "Failed to find va node");
    constraints.emplace_back(AndersConstraint::ADDR_OF, arg0Index, vaIndex);

    return true;
  }

  // POSIX threads:
  if (cs->getCalledFunction()->getName() == "pthread_create") {
    const Instruction *inst = cs;
    const Value* data = cs->getArgOperand(3);
    if (data == nullptr) return false; // Not always given data, e.g., globals.

    Function *routine = dyn_cast<Function>(cs->getArgOperand(2));
    if (routine == nullptr) return false; // Thread with no routine? Nonsense!

    NodeIndex argIndex = nodeFactory.getValueNodeFor(data, context);
    assert(argIndex != AndersNodeFactory::InvalidIndex && "Failed to find argIndex node");

    // If we are coming from a context, we keep it:
    // If we are coming from noContext, then we want to figure one out:
    // NodeIndex objIdx = (context == NoContext) ? functionCtxId : context;
    ContextType baseContext = (context == NoContext) ? funcCtxId : context;

    // If we are tracking an object, we can clone:
    std::optional<FunctionContext> functionContext = std::nullopt;
    if (baseContext != NoContext) {
      NodeIndex baseFunctionIdx = nodeFactory.getObjectNodeFor(routine, NoContext);

      // We may not actually need to clone if this already exists:
      if (!_contextMgr.doesFunctionContextExist(baseFunctionIdx, baseContext))
        scanFunction(routine, baseContext);

      // We only need the parameter list, which should be ordered...
      functionContext = _contextMgr.getFunctionContext(baseFunctionIdx, baseContext);
    }

    size_t argNum = std::distance(routine->args().begin(), routine->args().end());

    if (argNum >= 1) {
      NodeIndex paramIndex = functionContext->parameterIdxs[0];
      assert(paramIndex != AndersNodeFactory::InvalidIndex && "Failed to find paramIndex node");
      constraints.emplace_back(AndersConstraint::COPY, paramIndex, argIndex);
      addConstraint(AndersConstraint::COPY, data, paramIndex, argIndex, baseContext);
    }
    return true;
  }
  return false;
}
