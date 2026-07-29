#include "AndersenTestFixture.h"

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Simple") {
    parseAssembly(R"(
        define void @F1(ptr %ptr) {
            %load = load ptr, ptr %ptr
            ret void
        }

        define void @main() {
            %first = alloca ptr
            %second = alloca ptr

            %x = call ptr @get()
            store ptr %first, ptr %x
            call void @F1(ptr %x)

            %y = call ptr @get()
            store ptr %second, ptr %y
            call void @F1(ptr %y)
            ret void
        }

        declare ptr @get() #0
        attributes #0 = { allockind("alloc,uninitialized,aligned") allocsize(0) }
    )");

    const Value *first = findInstruction("main", "first");
    const Value *second = findInstruction("main", "second");

    const Value *load = findInstruction("F1", "load");
    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    assertPtsToExact(load, {first}, x);
    assertPtsToExact(load, {second}, y);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Chain_Two") {
    parseAssembly(R"(
        define void @F1(ptr %ptr) {
            call void @F2(ptr %ptr)
            ret void
        }

        define void @F2(ptr %ptrB) {
            %load = load ptr, ptr %ptrB
            ret void
        }

        define void @main() {
            %first = alloca ptr
            %second = alloca ptr

            %x = call ptr @get()
            store ptr %first, ptr %x
            call void @F1(ptr %x)

            %y = call ptr @get()
            store ptr %second, ptr %y
            call void @F1(ptr %y)
            ret void
        }

        declare ptr @get() #0
        attributes #0 = { allockind("alloc,uninitialized,aligned") allocsize(0) }
    )");

    const Value *first = findInstruction("main", "first");
    const Value *second = findInstruction("main", "second");

    const Value *load = findInstruction("F2", "load");
    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    assertPtsToExact(load, {first}, x);
    assertPtsToExact(load, {second}, y);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Global") {
    parseAssembly(R"(
        @g = global i32 0
        @h = global i32 0

        define void @F1(ptr %ptr) {
            ret void
        }

        define void @main() {
            call void @F1(ptr @g)
            call void @F1(ptr @h)
            ret void
        }
    )");

    const Value *formalArg = findParameter("F1", 0);
    const GlobalVariable *g = findGlobal("g");
    const GlobalVariable *h = findGlobal("h");
    assertPtsToExact(formalArg, {g}, g);
    assertPtsToExact(formalArg, {h}, h);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Global_Alias") {
    parseAssembly(R"(
        @g = global i32 0
        @a = alias i32, ptr @g

        define void @F1(ptr %ptr) {
            ret void
        }

        define void @main() {
            call void @F1(ptr @g)
            call void @F1(ptr @a)
            ret void
        }
    )");

    const Value *formalArg = findParameter("F1", 0);
    const GlobalVariable *g = findGlobal("g");
    const GlobalVariable *a = findGlobal("a");

    assertPtsToExact(formalArg, {g}, g);
    assertPtsToExact(formalArg, {g}, a);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Field_Sensitive_Simple") {
    parseAssembly(R"(
        %S = type { ptr, ptr }

        define void @F1(ptr %ptr) {
            %loadS = load ptr, ptr %ptr
            %field = getelementptr inbounds %S, ptr %loadS, i32 0, i32 1
            %load = load ptr, ptr %field
            ret void
        }

        define void @main() {
            %ptrA = call ptr @get()
            %ptrB = call ptr @get()

            %dataA = alloca %S
            %dataB = alloca %S

            %fieldA1 = getelementptr inbounds %S, ptr %dataA, i32 0, i32 1
            %fieldB1 = getelementptr inbounds %S, ptr %dataB, i32 0, i32 1

            %x = alloca i32
            %y = alloca i32

            store ptr %x, ptr %fieldA1
            store ptr %y, ptr %fieldB1

            store ptr %dataA, ptr %ptrA
            store ptr %dataB, ptr %ptrB

            call void @F1(ptr %ptrA)
            call void @F1(ptr %ptrB)
            ret void
        }

        declare ptr @get() #0
        attributes #0 = { allockind("alloc,uninitialized,aligned") allocsize(0) }
    )");

    const Value *ptrA = findInstruction("main", "ptrA");
    const Value *ptrB = findInstruction("main", "ptrB");
    const Value *load = findInstruction("F1", "load");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    assertPtsToExact(load, {x}, ptrA);
    assertPtsToExact(load, {y}, ptrB);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Field_Sensitive_Nested") {
    parseAssembly(R"(
        %S = type { ptr, ptr }
        %I = type { ptr, %S }

        define void @F1(ptr %ptr) {
            %loadI = load ptr, ptr %ptr
            %field = getelementptr inbounds %I, ptr %loadI, i32 0, i32 1, i32 1
            %load = load ptr, ptr %field
            ret void
        }

        define void @main() {
            %ptrA = call ptr @get()
            %ptrB = call ptr @get()

            %dataA = alloca %I
            %dataB = alloca %I

            %fieldA1 = getelementptr inbounds %I, ptr %dataA, i32 0, i32 1, i32 1
            %fieldB1 = getelementptr inbounds %I, ptr %dataB, i32 0, i32 1, i32 1

            %x = alloca i32
            %y = alloca i32

            store ptr %x, ptr %fieldA1
            store ptr %y, ptr %fieldB1

            store ptr %dataA, ptr %ptrA
            store ptr %dataB, ptr %ptrB

            call void @F1(ptr %ptrA)
            call void @F1(ptr %ptrB)
            ret void
        }

        declare ptr @get() #0
        attributes #0 = { allockind("alloc,uninitialized,aligned") allocsize(0) }
    )");

    const Value *ptrA = findInstruction("main", "ptrA");
    const Value *ptrB = findInstruction("main", "ptrB");
    const Value *load = findInstruction("F1", "load");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    assertPtsToExact(load, {x}, ptrA);
    assertPtsToExact(load, {y}, ptrB);
}
