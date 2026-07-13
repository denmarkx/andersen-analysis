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

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Extract_Value_Global_Struct") {
    parseAssembly(R"(
        @struct = global { ptr, ptr } { ptr @F1, ptr @F2 } 

        define { ptr, ptr } @Function() {
            %load = load { ptr, ptr }, ptr @struct
            ret { ptr, ptr } %load
        }

        define void @main() {
            %ptr = call { ptr, ptr } @Function()
            %fieldA = extractvalue { ptr, ptr } %ptr, 0
            %fieldB = extractvalue { ptr, ptr } %ptr, 1
            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
    )");

    const Value *F1 = findFunction("F1");
    const Value *F2 = findFunction("F2");

    const Value *ptr = findInstruction("main", "ptr");
    const Value *load = findInstruction("Function", "load");
    const Value *fieldA = findInstruction("main", "fieldA");
    const Value *fieldB = findInstruction("main", "fieldB");

    assertPtsToSetEmpty(load);
    assertPtsToSetEmpty(ptr);
    assertPtsToSetSize(fieldA, 1);
    assertPtsToSetSize(fieldB, 1);
    assertPtsToContains(fieldA, F1);
    assertPtsToContains(fieldB, F2);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Extract_Value_Global_Nested_Struct") {
    parseAssembly(R"(
        %Struct = type { ptr, { ptr, ptr } }
        @struct = global %Struct { ptr @F1, { ptr, ptr } { ptr @F2, ptr @F3 } } 

        define void @main() {
            %load = load %Struct, ptr @struct
            %fieldA = extractvalue %Struct %load, 0
            %fieldB = extractvalue %Struct %load, 1, 0
            %fieldC = extractvalue %Struct %load, 1, 1
            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
        define void @F3() { ret void }
    )");

    const Value *F1 = findFunction("F1");
    const Value *F2 = findFunction("F2");
    const Value *F3 = findFunction("F3");

    const Value *load = findInstruction("main", "load");
    const Value *fieldA = findInstruction("main", "fieldA");
    const Value *fieldB = findInstruction("main", "fieldB");
    const Value *fieldC = findInstruction("main", "fieldC");

    assertPtsToSetEmpty(load);
    assertPtsToSetSize(fieldA, 1);
    assertPtsToSetSize(fieldB, 1);
    assertPtsToSetSize(fieldC, 1);

    assertPtsToContains(fieldA, F1);
    assertPtsToContains(fieldB, F2);
    assertPtsToContains(fieldC, F3);
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Nested_Extract_Value_Global_Struct") {
    parseAssembly(R"(
        %Struct = type { ptr, { ptr, ptr } }
        @struct = global %Struct { ptr @F1, { ptr, ptr } { ptr @F2, ptr @F3 } } 

        define void @main() {
            %load = load %Struct, ptr @struct
            %fieldA = extractvalue %Struct %load, 1
            %fieldB = extractvalue { ptr, ptr } %fieldA, 1
            %fieldC = extractvalue %Struct %load, 1, 0
            %fieldD = extractvalue %Struct %load, 0
            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
        define void @F3() { ret void }
    )");

    const Value *F1 = findFunction("F1");
    const Value *F2 = findFunction("F2");
    const Value *F3 = findFunction("F3");

    const Value *load = findInstruction("main", "load");
    const Value *fieldA = findInstruction("main", "fieldA");
    const Value *fieldB = findInstruction("main", "fieldB");
    const Value *fieldC = findInstruction("main", "fieldC");
    const Value *fieldD = findInstruction("main", "fieldD");

    assertPtsToSetEmpty(load);
    assertPtsToSetEmpty(fieldA);
    assertPtsToExact(fieldB, {F3});
    assertPtsToExact(fieldC, {F2});
    assertPtsToExact(fieldD, {F1});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Extract_Value_W_NonPtrs") {
    parseAssembly(R"(
        %Struct = type { ptr, i32, ptr }
        @struct = global %Struct { ptr @F1, i32 1, ptr @F2 }

        define void @main() {
            %load = load %Struct, ptr @struct
            %fieldA = extractvalue %Struct %load, 0
            %fieldB = extractvalue %Struct %load, 1
            %fieldC = extractvalue %Struct %load, 2
            ret void
        }

        define void @F1() { ret void }
        define void @F2() { ret void }
    )");

    const Value *F1 = findFunction("F1");
    const Value *F2 = findFunction("F2");

    const Value *load = findInstruction("main", "load");
    const Value *fieldA = findInstruction("main", "fieldA");
    const Value *fieldB = findInstruction("main", "fieldB");
    const Value *fieldC = findInstruction("main", "fieldC");

    assertPtsToSetEmpty(load);
    assertPtsToSetEmpty(fieldB); // i32
    assertPtsToExact(fieldA, {F1});
    assertPtsToExact(fieldC, {F2});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Insert_Value_Simple") {
    // There are actually two aggregates here because chained IV doesn't mutate.
    // ..so: aggregateO = { %x, poison }
    //       aggregateU = { %x, %y }
    parseAssembly(R"(
        define void @main() {
            %x = alloca ptr
            %y = alloca ptr

            %aggregateO = insertvalue { ptr, ptr } poison, ptr %x, 0
            %aggregateU = insertvalue { ptr, ptr } %aggregateO, ptr %y, 1

            %extractO_0 = extractvalue { ptr, ptr } %aggregateO, 0 ; x
            %extractO_1 = extractvalue { ptr, ptr } %aggregateO, 1 ; poison

            %extractU_0 = extractvalue { ptr, ptr } %aggregateU, 0 ; x
            %extractU_1 = extractvalue { ptr, ptr } %aggregateU, 1 ; y
            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    const Value *aggregateO = findInstruction("main", "aggregateO");
    const Value *aggregateU = findInstruction("main", "aggregateU");

    const Value *extractO_0 = findInstruction("main", "extractO_0");
    const Value *extractO_1 = findInstruction("main", "extractO_1");

    const Value *extractU_0 = findInstruction("main", "extractU_0");
    const Value *extractU_1 = findInstruction("main", "extractU_1");

    assertPtsToSetEmpty(aggregateO);
    assertPtsToSetEmpty(aggregateU);
    assertPtsToSetEmpty(extractO_1);

    assertPtsToExact(extractO_0, {x});
    assertPtsToExact(extractU_0, {x});
    assertPtsToExact(extractU_1, {y});
}

TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Insert_Value_Nested") {
    parseAssembly(R"(
        %S = type { ptr, { ptr, ptr }}

        define void @main() {
            %x = alloca ptr
            %y = alloca ptr
            %z = alloca ptr

            %aggregateX = insertvalue %S poison, ptr %x, 0
            %aggregateY = insertvalue %S %aggregateX, ptr %y, 1, 0
            %aggregateZ = insertvalue %S %aggregateY, ptr %z, 1, 1

            %extractX_0 = extractvalue %S %aggregateX, 0
            %extractX_1_1 = extractvalue %S %aggregateX, 1, 0
            %extractX_1_2 = extractvalue %S %aggregateX, 1, 1

            %extractY_0 = extractvalue %S %aggregateY, 0
            %extractY_1_1 = extractvalue %S %aggregateY, 1, 0
            %extractY_1_2 = extractvalue %S %aggregateY, 1, 1

            %extractZ_0 = extractvalue %S %aggregateZ, 0
            %extractZ_1_1 = extractvalue %S %aggregateZ, 1, 0
            %extractZ_1_2 = extractvalue %S %aggregateZ, 1, 1
            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");
    const Value *z = findInstruction("main", "z");

    const Value *aggregateX = findInstruction("main", "aggregateX");
    const Value *aggregateY = findInstruction("main", "aggregateY");
    const Value *aggregateZ = findInstruction("main", "aggregateZ");

    const Value *extractX_0 = findInstruction("main", "extractX_0");
    const Value *extractX_1_1 = findInstruction("main", "extractX_1_1");
    const Value *extractX_1_2 = findInstruction("main", "extractX_1_2");

    const Value *extractY_0 = findInstruction("main", "extractY_0");
    const Value *extractY_1_1 = findInstruction("main", "extractY_1_1");
    const Value *extractY_1_2 = findInstruction("main", "extractY_1_2");

    const Value *extractZ_0 = findInstruction("main", "extractZ_0");
    const Value *extractZ_1_1 = findInstruction("main", "extractZ_1_1");
    const Value *extractZ_1_2 = findInstruction("main", "extractZ_1_2");

    assertPtsToSetEmpty(aggregateX);
    assertPtsToSetEmpty(aggregateY);
    assertPtsToSetEmpty(aggregateZ);

    assertPtsToExact(extractX_0, {x});
    assertPtsToSetEmpty(extractX_1_1);
    assertPtsToSetEmpty(extractX_1_2);
    
    assertPtsToExact(extractY_0, {x});
    assertPtsToExact(extractY_1_1, {y});
    assertPtsToSetEmpty(extractY_1_2);

    assertPtsToExact(extractZ_0, {x});
    assertPtsToExact(extractZ_1_1, {y});
    assertPtsToExact(extractZ_1_2, {z});
}


TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Insert_Value_Override") {
    // aggregateU is technically {{x, y}, poison} because flow-insensitivity.
    parseAssembly(R"(
        define void @main() {
            %x = alloca ptr
            %y = alloca ptr

            %aggregateO = insertvalue { ptr, ptr } poison, ptr %x, 0 ; {x, poison}
            %aggregateU = insertvalue { ptr, ptr } %aggregateO, ptr %y, 0 ; {y, poison}

            %extractO_0 = extractvalue { ptr, ptr } %aggregateO, 0
            %extractO_1 = extractvalue { ptr, ptr } %aggregateO, 1

            %extractU_0 = extractvalue { ptr, ptr } %aggregateU, 0
            %extractU_1 = extractvalue { ptr, ptr } %aggregateU, 1

            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    const Value *aggregateO = findInstruction("main", "aggregateO");
    const Value *aggregateU = findInstruction("main", "aggregateU");

    const Value *extractO_0 = findInstruction("main", "extractO_0");
    const Value *extractO_1 = findInstruction("main", "extractO_1");

    const Value *extractU_0 = findInstruction("main", "extractU_0");
    const Value *extractU_1 = findInstruction("main", "extractU_1");

    assertPtsToSetEmpty(aggregateO);
    assertPtsToSetEmpty(aggregateU);
    assertPtsToSetEmpty(extractO_1);
    assertPtsToSetEmpty(extractU_1);

    assertPtsToExact(extractO_0, {x});
    assertPtsToExact(extractU_0, {x, y});
}


TEST_CASE_FIXTURE(AndersenTestFixture, "FS_Insert_Value_Simple_Interprocedural") {
    // ptsTo(extractO_1) is going to return {y} only because there is no context sensitivity yet.
    parseAssembly(R"(
        define { ptr, ptr } @GetAggregateO(ptr %0) {
            %aggregate = insertvalue { ptr, ptr } poison, ptr %0, 0
            ret { ptr, ptr } %aggregate
        }

        define { ptr, ptr } @GetAggregateU(ptr %0, { ptr, ptr } %aggregateO) {
            %aggregate = insertvalue { ptr, ptr } %aggregateO, ptr %0, 1
            ret { ptr, ptr } %aggregate
        }

        define ptr @GetFirstElement({ptr, ptr} %aggregate) {
            %element = extractvalue { ptr, ptr } %aggregate, 0
            ret ptr %element
        }

        define ptr @GetSecondElement({ptr, ptr} %aggregate) {
            %element = extractvalue { ptr, ptr } %aggregate, 1
            ret ptr %element
        }

        define void @main() {
            %x = alloca ptr
            %y = alloca ptr

            %aggregateO = call { ptr, ptr } @GetAggregateO(ptr %x)
            %extractO_0 = call ptr @GetFirstElement({ptr, ptr} %aggregateO)
            %extractO_1 = call ptr @GetSecondElement({ptr, ptr} %aggregateO)

            %aggregateU = call { ptr, ptr } @GetAggregateU(ptr %y, { ptr, ptr } %aggregateO)
            %extractU_0 = call ptr @GetFirstElement({ptr, ptr} %aggregateU)
            %extractU_1 = call ptr @GetSecondElement({ptr, ptr} %aggregateU)

            ret void
        }
    )");

    const Value *x = findInstruction("main", "x");
    const Value *y = findInstruction("main", "y");

    const Value *aggregateO = findInstruction("main", "aggregateO");
    const Value *aggregateU = findInstruction("main", "aggregateU");

    const Value *extractO_0 = findInstruction("main", "extractO_0");
    const Value *extractO_1 = findInstruction("main", "extractO_1");

    const Value *extractU_0 = findInstruction("main", "extractU_0");
    const Value *extractU_1 = findInstruction("main", "extractU_1");
    andersen->printPointsToSet(extractO_0);
    andersen->printPointsToSet(extractO_1);
    andersen->printPointsToSet(extractU_0);
    andersen->printPointsToSet(extractU_1);

    assertPtsToSetEmpty(aggregateO);
    assertPtsToSetEmpty(aggregateU);

    assertPtsToExact(extractO_0, {x});
    assertPtsToExact(extractO_1, {y}); // NOTE: once context sensitivity exists, this is supposed to be {}.
    assertPtsToExact(extractU_0, {x});
    assertPtsToExact(extractU_1, {y});
}
