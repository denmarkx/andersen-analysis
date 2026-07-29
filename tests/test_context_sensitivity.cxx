#include "AndersenTestFixture.h"

TEST_CASE_FIXTURE(AndersenTestFixture, "Context_Object_Sensitivity_Simple") {
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
