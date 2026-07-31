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

    assertPtsToExact(load, {first}, {x});
    assertPtsToExact(load, {second}, {y});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Selective_Parameter_Contexts") {
    parseAssembly(R"(
        define void @F1(i32 %0, ptr %ptr, i32 %1) {
            %load = load ptr, ptr %ptr
            ret void
        }

        define void @main() {
            %first = alloca ptr
            %second = alloca ptr

            %x = call ptr @get()
            store ptr %first, ptr %x

            %y = call ptr @get()
            store ptr %second, ptr %y

            call void @F1(i32 1, ptr %x, i32 2)
            call void @F1(i32 3, ptr %y, i32 4)
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

    assertPtsToExact(load, {first}, {x});
    assertPtsToExact(load, {second}, {y});
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

    assertPtsToExact(load, {first}, {x});
    assertPtsToExact(load, {second}, {y});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Global") {
    parseAssembly(R"(
        @g = global i32 0
        @h = global i32 0

        define void @F1(ptr %ptr) {
            %x = load ptr, ptr %ptr
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
    assertPtsToExact(formalArg, {g}, {g});
    assertPtsToExact(formalArg, {h}, {h});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Global_Alias") {
    parseAssembly(R"(
        @g = global i32 0
        @a = alias i32, ptr @g

        define void @F1(ptr %ptr) {
            %x = load ptr, ptr %ptr
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
    const GlobalAlias *a = findGlobalAlias("a");

    assertPtsToExact(formalArg, {g}, {g});
    assertPtsToExact(formalArg, {g}, {a});
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

    assertPtsToExact(load, {x}, {ptrA});
    assertPtsToExact(load, {y}, {ptrB});
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

    assertPtsToExact(load, {x}, {ptrA});
    assertPtsToExact(load, {y}, {ptrB});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Rust_Clone") {
    parseAssembly(R"(
        ; taken from Arc<T>::clone
        define internal ptr @Clone(ptr align 8 %self) {
            start:
              %0 = alloca i64, align 8
              %_0 = alloca ptr, align 8
              %self1 = load ptr, ptr %self, align 8
              %1 = atomicrmw add ptr %self1, i64 1 monotonic, align 8
              store i64 %1, ptr %0, align 8
              %old_size = load i64, ptr %0, align 8
              %_4 = icmp ugt i64 %old_size, 9223372036854775807
              br label %bb1

            bb1:
              %ptr = load ptr, ptr %self, align 8
              %_8 = getelementptr i8, ptr %self, i64 8
              store ptr %ptr, ptr %_0, align 8
              %2 = load ptr, ptr %_0, align 8
              ret ptr %2
            }

        define void @main() {
            %ptr = call ptr @New(ptr null)
            %x = alloca ptr, align 8
            store ptr %ptr, ptr %x

            %y = call ptr @Clone(ptr %x)

            %ptr2 = call ptr @New(ptr null)
            %z = alloca ptr, align 8
            store ptr %ptr2, ptr %z

            %w = call ptr @Clone(ptr %z)
            ret void
        }

        declare ptr @New(ptr align 4) unnamed_addr #0

        attributes #0 = { inlinehint nonlazybind allockind("alloc") uwtable }
    )");

    const llvm::Value *ptr = findInstruction("main", "ptr");
    const llvm::Value *y = findInstruction("main", "y");

    const llvm::Value *ptr2 = findInstruction("main", "ptr2");
    const llvm::Value *w = findInstruction("main", "w");

    assertPtsToExact(y, {ptr});
    assertPtsToExact(w, {ptr2});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "COS_Chain_Two_Distinct_Entry") {
    parseAssembly(R"(
        define void @F1(ptr %ptr) {
            call void @F2(ptr %ptr)
            ret void
        }

        define void @F2(ptr %ptrB) {
            %load = load ptr, ptr %ptrB
            ret void
        }

        define void @entryA() {
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

        define void @entryB() {
            %third = alloca ptr
            %fourth = alloca ptr

            %x = call ptr @get()
            store ptr %third, ptr %x
            call void @F1(ptr %x)

            %y = call ptr @get()
            store ptr %fourth, ptr %y
            call void @F1(ptr %y)
            ret void
        }

        define void @entryC() {
            %fifth = alloca ptr

            %x = call ptr @get()
            store ptr %fifth, ptr %x
            call void @F2(ptr %x)
            ret void
        }

        declare ptr @get() #0
        attributes #0 = { allockind("alloc,uninitialized,aligned") allocsize(0) }
    )");

    const Value *first = findInstruction("entryA", "first");
    const Value *second = findInstruction("entryA", "second");
    const Value *xA = findInstruction("entryA", "x");
    const Value *yA = findInstruction("entryA", "y");

    const Value *third = findInstruction("entryB", "third");
    const Value *fourth = findInstruction("entryB", "fourth");
    const Value *xB = findInstruction("entryB", "x");
    const Value *yB = findInstruction("entryB", "y");

    const Value *fifth = findInstruction("entryC", "fifth");
    const Value *xC = findInstruction("entryC", "x");

    const Value *load = findInstruction("F2", "load");

    assertPtsToExact(load, {first}, {xA});
    assertPtsToExact(load, {second}, {yA});

    assertPtsToExact(load, {third}, {xB});
    assertPtsToExact(load, {fourth}, {yB});

    assertPtsToExact(load, {fifth}, {xC});
}
