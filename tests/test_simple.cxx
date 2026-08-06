#include "Andersen.h"
#include "ContextManager.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "AndersenTestFixture.h"

TEST_CASE_FIXTURE(AndersenTestFixture, "Alloca_Store_Load_Stack") {
    parseAssembly(R"(
        define void @main() {
            %ptr = alloca ptr
            %x = alloca i32
            store ptr %x, ptr %ptr
            %load = load ptr, ptr %ptr
            ret void
        }
    )");

    const Value *ptr = findInstruction("main", "ptr");
    const Value *x = findInstruction("main", "x");
    const Value *load = findInstruction("main", "load");
    assertPtsToSetSize(ptr, 2);
    assertPtsToSetSize(x, 1);
    assertPtsToContains(load, x);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Unrelated_Allocas") {
    parseAssembly(R"(
        define void @main() {
            %ptrA = alloca ptr
            %ptrB = alloca ptr
            ret void
        }
    )");

    const Value *ptrA = findInstruction("main", "ptrA");
    const Value *ptrB = findInstruction("main", "ptrB");
    assertPtsToSetSize(ptrA, 1);
    assertPtsToSetSize(ptrB, 1);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Double_Indirection") {
    parseAssembly(R"(
        define void @main() {
            %x = alloca i32
            %ptrA = alloca ptr
            %ptrB = alloca ptr

            store ptr %x, ptr %ptrA
            store ptr %ptrA, ptr %ptrB

            %loadB = load ptr, ptr %ptrB
            %loadA = load ptr, ptr %loadB
            ret void
        }
    )");

    const Value *ptrA = findInstruction("main", "ptrA");
    const Value *ptrB = findInstruction("main", "ptrB");
    const Value *x = findInstruction("main", "x");
    const Value *loadA = findInstruction("main", "loadA");
    const Value *loadB = findInstruction("main", "loadB");
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Null_No_Alias") {
    parseAssembly(R"(
        define void @main() {
            %ptrA = alloca ptr
            store ptr null, ptr %ptrA
            %load = load ptr, ptr %ptrA
            ret void
        }
    )");

    const Value *ptrA = findInstruction("main", "ptrA");
    const Value *load = findInstruction("main", "load");
    assertPtsToSetSize(ptrA, 1);
    assertPtsToSetSize(load, 0);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Call_Return") {
    parseAssembly(R"(
        define ptr @F1(ptr %arg) {
            ret ptr %arg
        }

        define void @main() {
            %ptr = alloca i32
            %retval = call ptr @F1(ptr %ptr)
            ret void
        }
    )");

    const Value *ptrA = findInstruction("main", "ptr");
    const Value *retval = findInstruction("main", "retval");

    assertPtsToSetSize(ptrA, 1);
    assertPtsToSetSize(retval, 1);
    assertPtsToContains(retval, ptrA);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "PHI") {
    parseAssembly(R"(
        define void @main(i1 %cond) {
            entry:
                %x = alloca i32
                %y = alloca i32
                br i1 %cond, label %left, label %right

            left:
                br label %next

            right:
                br label %next

            next:
                %ptr = phi ptr [ %x, %left ], [ %y, %right ]
            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");
    const Value *ptr = findInstruction("main", "ptr");

    assertPtsToSetSize(x, 1);
    assertPtsToSetSize(y, 1);

    // Since this is flow-insensitive, ptsTo(ptr) = { x[O], y[O] }
    assertPtsToContains(ptr, x);
    assertPtsToContains(ptr, y);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Global_Integer") {
    parseAssembly(R"(
        @count = global i32 0

        define void @main() {
            %ptr = alloca ptr
            store i32 100, ptr @count
            store ptr @count, ptr %ptr

            %load = load ptr, ptr %ptr
            %value = load i32, ptr %load
            ret void
        }
    )");

    const Value *count = findGlobal("count");
    const Value *ptr = findInstruction("main", "ptr");
    const Value *load = findInstruction("main", "load");
    const Value *value = findInstruction("main", "value");

    assertPtsToSetSize(count, 1);
    assertPtsToSetEmpty(value);
    assertPtsToContains(load, count);
    assertPtsToContains(ptr, count);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Global_Alias") {
    parseAssembly(R"(
        @count = global i32 0
        @alias = alias i32, ptr @count

        define void @main() {
            %ptr = alloca ptr
            store ptr @alias, ptr %ptr

            %load = load ptr, ptr %ptr
            ret void
        }
    )");

    const Value *count = findGlobal("count");
    const Value *alias = findGlobal("alias");

    const Value *ptr = findInstruction("main", "ptr");
    const Value *load = findInstruction("main", "load");

    assertPtsToSetSize(count, 1);
    assertPtsToContains(load, count);
    assertPtsToContains(ptr, count);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Global_Nested_Alias") {
    // All aliases follow back to @count
    // ..which also means that @aliasA,B,C are NOT values and NOT objects internally.
    // store ptr @aliasA,B,C,.. is treated as store ptr @count,..
    parseAssembly(R"(
        @count = global i32 0
        @aliasA = alias i32, ptr @count
        @aliasB = alias i32, ptr @aliasA
        @aliasC = alias i32, ptr @aliasB

        define void @main() {
            %ptrA = alloca ptr
            store ptr @aliasA, ptr %ptrA
            %loadA = load ptr, ptr %ptrA

            %ptrB = alloca ptr
            store ptr @aliasB, ptr %ptrB
            %loadB = load ptr, ptr %ptrB

            %ptrC = alloca ptr
            store ptr @aliasC, ptr %ptrC
            %loadC = load ptr, ptr %ptrC
            ret void
        }
    )");

    const Value *count = findGlobal("count");
    const Value *aliasA = findGlobal("aliasA");
    const Value *aliasB = findGlobal("aliasB");
    const Value *aliasC = findGlobal("aliasC");

    const Value *ptrA = findInstruction("main", "ptrA");
    const Value *ptrB = findInstruction("main", "ptrB");
    const Value *ptrC = findInstruction("main", "ptrC");

    const Value *loadA = findInstruction("main", "loadA");
    const Value *loadB = findInstruction("main", "loadB");
    const Value *loadC = findInstruction("main", "loadC");

    assertPtsToSetSize(count, 1);

    assertPtsToExact(loadA, {count});
    assertPtsToContains(ptrA, count);

    assertPtsToExact(loadB, {count});
    assertPtsToContains(ptrB, count);

    assertPtsToExact(loadC, {count});
    assertPtsToContains(ptrC, count);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Indirect_Call_From_Global_Function_Pointer") {
    parseAssembly(R"(
        define void @target() {
            ret void
        }

        @func_pointer = global ptr @target

        define void @main() {
            %function = load ptr, ptr @func_pointer
            call void %function()
            ret void
        }
    )");

    const Value *func_pointer = findGlobal("func_pointer");
    const Value *function = findInstruction("main", "function");
    const Value *target = findFunction("target");

    assertPtsToSetSize(func_pointer, 2);
    assertPtsToSetSize(target, 1);
    assertPtsToSetSize(function, 1);
    assertPtsToContains(function, target);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "Gen_NoErrorOnAbsentPtr") {
    parseAssembly(R"(
        define void @main() {
            %x = alloca ptr

            %v = alloca ptr
            store ptr %v, ptr %x

            %y = alloca i32
            store i32 5, ptr %y

            %load_x = load ptr, ptr %x ; {v}
            %load_y = load ptr, ptr %y ; {}
            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");
    const Value *v = findInstruction("main", "v");
    const Value *load_x = findInstruction("main", "load_x");
    const Value *load_y = findInstruction("main", "load_y");

    PtsSetType ptsSetLX;
    andersen->getPointsToSet(load_x, ptsSetLX, NoContext);

    PtsSetType ptsSetLY;
    andersen->getPointsToSet(load_y, ptsSetLY, NoContext);

    PtsSetType ptsSet;
    andersen->getPointsToSet(nullptr, ptsSet, NoContext);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "General_Loop_Like") {
    parseAssembly(R"(
        define void @main() {
        entry:
            %container = alloca ptr
            %a = alloca i32
            %b = alloca i32
            %c = alloca i32
            br label %body

        body:
            store ptr %a, ptr %container
            store ptr %b, ptr %container
            store ptr %c, ptr %container
            br label %exit

        exit:
            %load = load ptr, ptr %container
            ret void
        }
    )");

    const Value *a = findInstruction("main", "a");
    const Value *b = findInstruction("main", "b");
    const Value *c = findInstruction("main", "c");
    const Value *load = findInstruction("main", "load");

    assertPtsToSetSize(load, 3);
    assertPtsToContains(load, a);
    assertPtsToContains(load, b);
    assertPtsToContains(load, c);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "General_IntToPtr_PtrToInt") {
    parseAssembly(R"(
        define void @main() {
            %x = alloca i32
            %i = ptrtoint ptr %x to i64
            %y = inttoptr i64 %i to ptr
            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    assertPtsToExact(y, {x});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "General_IntToPtr_Opaque") {
    // y = inttoptr then some i64 expr, this should theoretically go to set U..
    parseAssembly(R"(
        define void @main(i64 %raw) {
            %x = add i64 %raw, 8
            %y = inttoptr i64 %x to ptr
            ret void
        }
    )");

    const Value *y = findInstruction("main", "y");

    // the main thing here is to ensure this doesnt crash.
    PtsSetType pts;
    andersen->getPointsToSet(y, pts, NoContext);
}
