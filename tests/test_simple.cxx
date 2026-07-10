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
    // assertPtsToSetEmpty(value); TODO: this asserts on mergetarget..but it should just ret {}
    assertPtsToContains(load, count);
    assertPtsToContains(ptr, count);
}

// TODO: aliases are improperly implemented.
// they should not create a new abstract object
// and instead take the addr of the ptr
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

    // const Value *ptr = findInstruction("main", "ptr");
    // const Value *load = findInstruction("main", "load");

    // assertPtsToSetSize(count, 1);
    // assertPtsToContains(load, count);
    // assertPtsToContains(ptr, count);
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
