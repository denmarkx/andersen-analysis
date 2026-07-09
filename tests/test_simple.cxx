#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "include/doctest.h"

#include "AndersenTestFixture.h"

TEST_CASE_FIXTURE(AndersenTestFixture, "test") {
    parseAssembly(R"(
        define void @main() {
            ret void;
        }
    )");

    errs() << *getFunction("main") << "\n";
}