#include "AndersenTestFixture.h"

TEST_CASE_FIXTURE(AndersenTestFixture, "TFF_RustThreadMutexArcPipeline") {
    parseFile("tests/files/Test_ThreadMutexArc.ll", false);

    const Function *vtable = findFunction("vtable_shim");
    const Function *builder_last = findFunction("ThreadSpawn_Unchecked2");

    // We can assume that ThreadSpawn_Unchecked2 has the identified thread spawn call and that:
    const Value *thread_data = findInstruction("ThreadSpawn_Unchecked2", "thread_data");
    andersen->addFunction(vtable, builder_last, {{thread_data, 0}});
    andersen->runConstraintSolver();

    // What I really care about is if we can get user_data (from CreateMutex)
    // which is piped through a struct to the vtable shim and to some user function on our psuedo-"thread".
    const Value *x = findInstruction("main", "x");
    const Value *data = findInstruction("CreateMutex", "user_data");
    const Value *load = findInstruction("user_routine", "load");
    assertPtsToExact(load, {data}, {x});
}
