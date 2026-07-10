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