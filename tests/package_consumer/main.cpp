#include <print>

#include <tinywfc/tinywfc.h>

int main()
{
   // a checkerboard of two tiles on an 8 x 8 grid
   tinywfc::Rules rules{2};
   rules.allow(0, tinywfc::Direction::PositiveX, 1);
   rules.allow(1, tinywfc::Direction::PositiveX, 0);
   rules.allow(0, tinywfc::Direction::PositiveZ, 1);
   rules.allow(1, tinywfc::Direction::PositiveZ, 0);
   auto solver = tinywfc::Solver::create(rules, {._size = {8, 1, 8}, ._seed = 1});
   if (!solver || !solver->run())
   {
      return 1;
   }
   std::println("consumer ok: {} cells", solver->result().size());
   return 0;
}
