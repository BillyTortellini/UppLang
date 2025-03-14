#include "test.hpp"

#include "../../upplib.hpp"
#include "../upp_lang/compiler.hpp"

struct Wombat
{
	int values[100];
};

void test_entry()
{
	Wombat* wombat = nullptr;
	for (int i = 0; i < 10; i++)
	{
		wombat = new Wombat;

		int z = 10;
		z += 1;

		delete wombat;
	}


	compiler_initialize();
	Compilation_Unit* unit = compiler_add_compilation_unit(string_create_static("upp_code/allocators.upp"), true, false);
	compiler_compile(unit, Compile_Type::BUILD_CODE);
	compiler_run_testcases(true);
	compiler_destroy();

	printf("Hello world\n");
}
