#include "AndersenTestFixture.h"

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Simple") {
    parseAssembly(R"(
        %S = type { ptr, ptr }

        define void @main() {
            %ptr = alloca %S

            %x = alloca i32
            %y = alloca i32

            %s1 = getelementptr inbounds %S, ptr %ptr, i32 0, i32 0
            store ptr %x, ptr %s1

            %s2 = getelementptr inbounds %S, ptr %ptr, i32 0, i32 1
            store ptr %y, ptr %s2

            %loadS1 = load ptr, ptr %s1
            %loadS2 = load ptr, ptr %s2

            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    const Value *loadS1 = findInstruction("main", "loadS1");
    const Value *loadS2 = findInstruction("main", "loadS2");

    assertPtsToSetSize(loadS1, 1); // x
    assertPtsToSetSize(loadS2, 1); // y
    assertPtsToSetSize(x, 1); // x [O]
    assertPtsToSetSize(y, 1); // y [O]

    assertPtsToContains(loadS1, x);
    assertPtsToContains(loadS2, y);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Simple_Nested") {
    parseAssembly(R"(
        %S = type { [8 x ptr], ptr }
        %T = type { [8 x [8 x ptr] ], ptr }

        define void @main() {
            %ptr = alloca %S

            %x = alloca i32
            %y = alloca i32

            %s1 = getelementptr inbounds %T, ptr %ptr, i32 0, i32 0, i32 5, i32 2
            store ptr %x, ptr %s1

            %s2 = getelementptr inbounds %T, ptr %ptr, i32 0, i32 0, i32 2, i32 7
            store ptr %y, ptr %s2

            %loadS1 = load ptr, ptr %s1
            %loadS2 = load ptr, ptr %s2

            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    const Value *loadS1 = findInstruction("main", "loadS1");
    const Value *loadS2 = findInstruction("main", "loadS2");

    assertPtsToSetSize(loadS1, 1); // x
    assertPtsToSetSize(loadS2, 1); // y
    assertPtsToSetSize(x, 1); // x [O]
    assertPtsToSetSize(y, 1); // y [O]

    assertPtsToContains(loadS1, x);
    assertPtsToContains(loadS2, y);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Simple_Interprocedural_Direct") {
    parseAssembly(R"(
        %S = type { ptr, ptr }

        define void @F1(ptr %0, ptr %1) {
            %s1 = getelementptr inbounds %S, ptr %0, i32 0, i32 0
            store ptr %1, ptr %s1
            %loadS1 = load ptr, ptr %s1
            ret void
        }

        define void @F2(ptr %0) {
            %y = alloca i32

            %s2 = getelementptr inbounds %S, ptr %0, i32 0, i32 1
            store ptr %y, ptr %s2
            %loadS2 = load ptr, ptr %s2
            ret void
        }

        define void @main() {
            %ptr = alloca %S

            %x = alloca i32

            call void @F1(ptr %ptr, ptr %x)
            call void @F2(ptr %ptr)
            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("F2", "y");

    const Value *loadS1 = findInstruction("F1", "loadS1");
    const Value *loadS2 = findInstruction("F2", "loadS2");

    assertPtsToSetSize(x, 1);
    assertPtsToSetSize(y, 1);
    assertPtsToSetSize(loadS1, 1);
    assertPtsToSetSize(loadS2, 1);

    assertPtsToContains(loadS1, x);
    assertPtsToContains(loadS2, y);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Simple_Interprocedural_GEP_Parameter") {
    parseAssembly(R"(
        %S = type { ptr, ptr }

        define void @F1(ptr %0) {
            %loadF1 = load ptr, ptr %0

            call void @F2(ptr %0)
            ret void
        }

        define void @F2(ptr %0) {
            %loadF2 = load ptr, ptr %0
            ret void
        }

        define void @main() {
            %ptr = alloca %S
            %x = alloca i32
            %y = alloca i32

            %s1 = getelementptr inbounds %S, ptr %ptr, i32 0, i32 0
            store ptr %x, ptr %s1

            %s2 = getelementptr inbounds %S, ptr %ptr, i32 0, i32 0
            store ptr %y, ptr %s2

            call void @F1(ptr %s1)
            call void @F1(ptr %s2)
            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    const Value *loadF1 = findInstruction("F1", "loadF1");
    const Value *loadF2 = findInstruction("F2", "loadF2");

    assertPtsToSetSize(x, 1);
    assertPtsToSetSize(y, 1);

    assertPtsToSetSize(loadF1, 2);
    assertPtsToContains(loadF1, x);
    assertPtsToContains(loadF1, y);

    assertPtsToSetSize(loadF2, 2);
    assertPtsToContains(loadF2, x);
    assertPtsToContains(loadF2, y);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Global_Array_of_Functions") {
    parseAssembly(R"(
        @array = global [3 x ptr] [ptr @F1, ptr @F2, ptr @F3]

        define void @main() {
            %s1 = getelementptr inbounds [3 x ptr], ptr @array, i32 0, i32 1
            %loadS1 = load ptr, ptr %s1

            %s2 = getelementptr inbounds [3 x ptr], ptr @array, i32 0, i32 2
            store ptr @F4, ptr %s2
            %loadS2 = load ptr, ptr %s2

            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
        define void @F3() { ret void }
        define void @F4() { ret void }
    )");

    const Function *F1 = findFunction("F1");
    const Function *F2 = findFunction("F2");
    const Function *F3 = findFunction("F3");
    const Function *F4 = findFunction("F4");

    const Value *loadS1 = findInstruction("main", "loadS1");
    const Value *loadS2 = findInstruction("main", "loadS2");

    assertPtsToSetSize(F1, 1);
    assertPtsToSetSize(F2, 1);
    assertPtsToSetSize(F3, 1);
    assertPtsToSetSize(F4, 1);

    assertPtsToSetSize(loadS1, 1); // F2
    assertPtsToSetSize(loadS2, 2); // F3, F4

    assertPtsToContains(loadS1, F2);
    assertPtsToContains(loadS2, F3);
    assertPtsToContains(loadS2, F4);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Global_Nested") {
    parseAssembly(R"(
        @array = global [2 x [2 x ptr]] [
            [2 x ptr] [ptr @F1, ptr @F2],
            [2 x ptr] [ptr @F3, ptr @F4]
        ]

        define void @main() {
            %x = alloca i32
            %y = alloca i32

            %s1 = getelementptr inbounds [2 x [2 x ptr]], ptr @array, i32 0, i32 0, i32 1
            %s2 = getelementptr inbounds [2 x [2 x ptr]], ptr @array, i32 0, i32 1, i32 0

            %loadS1 = load ptr, ptr %s1
            %loadS2 = load ptr, ptr %s2

            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
        define void @F3() { ret void }
        define void @F4() { ret void }
    )");

    const Function *F1 = findFunction("F1");
    const Function *F2 = findFunction("F2");
    const Function *F3 = findFunction("F3");
    const Function *F4 = findFunction("F4");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    const Value *loadS1 = findInstruction("main", "loadS1");
    const Value *loadS2 = findInstruction("main", "loadS2");

    assertPtsToSetSize(F1, 1);
    assertPtsToSetSize(F2, 1);
    assertPtsToSetSize(F3, 1);
    assertPtsToSetSize(F4, 1);

    assertPtsToSetSize(x, 1);
    assertPtsToSetSize(y, 1);

    assertPtsToContains(loadS1, F2);
    assertPtsToContains(loadS2, F3);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_GEP_Expression") {
    parseAssembly(R"(
        @array = global [2 x ptr] [ptr @F1, ptr @F2], align 8

        define void @main() {
            %load = load ptr, ptr getelementptr inbounds ([2 x ptr], ptr @array, i32 0, i32 1)
            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
    )");

    const Function *F1 = findFunction("F1");
    const Function *F2 = findFunction("F2");

    const Value *load = findInstruction("main", "load");

    assertPtsToSetSize(F1, 1);
    assertPtsToSetSize(F2, 1);
    assertPtsToSetSize(load, 1);
    assertPtsToContains(load, F2);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Nested_GEP_Expression") {
    parseAssembly(R"(
        @array = global [2 x [2 x ptr]] [
            [2 x ptr] [ptr @F1, ptr @F2],
            [2 x ptr] [ptr @F3, ptr @F4]
        ]

        define void @main() {
            %loadA = load ptr, ptr getelementptr ([2 x [2 x ptr]], ptr @array, i64 0, i64 1, i64 1)

            %s1 = getelementptr [2 x [2 x ptr]], ptr @array, i64 0, i64 1, i64 1

            ; Equivalent to loadA.
            ; Note: constant expr GEPs whose pointer operand reference an SSA
            ;       instr do not exist (which is why loadB doesnt do a gep).   
            %loadB = load ptr, ptr %s1
            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
        define void @F3() { ret void }
        define void @F4() { ret void }
    )");

    const Function *F1 = findFunction("F1");
    const Function *F2 = findFunction("F2");
    const Function *F3 = findFunction("F3");
    const Function *F4 = findFunction("F4");

    const Value *loadA = findInstruction("main", "loadA");
    const Value *loadB = findInstruction("main", "loadB");

    assertPtsToSetSize(F1, 1);
    assertPtsToSetSize(F2, 1);
    assertPtsToSetSize(F3, 1);
    assertPtsToSetSize(F4, 1);

    assertPtsToSetSize(loadA, 1);
    assertPtsToSetSize(loadB, 1);

    assertPtsToContains(loadA, F4);
    assertPtsToContains(loadB, F4);

}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Pointer_Offset") {
    parseAssembly(R"(
        %S = type { ptr, ptr }
        %T = type { %S, %S }

        define void @main() {
            %ptr = alloca %S
            %x = alloca i32

            ; i64 1: move by sizeof(%S)
            ; i32 1: second pointer field within %S
            ; ..equivalent to: ptr %ptr, i32 0, i32 1, i32 1
            %s1 = getelementptr %S, ptr %ptr, i64 1, i32 1
            store ptr %x, ptr %s1
            %loadS1 = load ptr, ptr %s2

            ; ..equivalent to s1:
            %s2 = getelementptr %T, ptr %ptr, i32 0, i32 1, i32 1
            %loadS2 = load ptr, ptr %s2

            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *loadS1 = findInstruction("main", "loadS1");
    const Value *loadS2 = findInstruction("main", "loadS2");

    assertPtsToSetSize(x, 1);
    assertPtsToSetSize(loadS1, 1);
    assertPtsToSetSize(loadS2, 1);

    assertPtsToContains(loadS1, x);
    assertPtsToContains(loadS2, x);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Byte_Offset") {
    parseAssembly(R"(
        %S = type { [64 x i8] }

        define void @main() {
            %ptr = alloca %S
            %x = alloca i8

            ; First field: [64 x i8][0]
            %base = getelementptr inbounds %S, ptr %ptr, i32 0, i32 0

            ; Move 8 bytes: &S->field[1]
            %s1 = getelementptr inbounds i8, ptr %base, i64 1
            store ptr %x, ptr %s1
            %loadS1 = load ptr, ptr %s1

            ; Equivalent to %s1
            %s2 = getelementptr inbounds %S, ptr %ptr, i32 0, i32 0, i32 1
            %loadS2 = load ptr, ptr %s2

            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *loadS1 = findInstruction("main", "loadS1");
    const Value *loadS2 = findInstruction("main", "loadS2");

    assertPtsToSetSize(x, 1);
    assertPtsToSetSize(loadS1, 1);
    assertPtsToSetSize(loadS2, 1);

    assertPtsToContains(loadS1, x);
    assertPtsToContains(loadS2, x);
}
