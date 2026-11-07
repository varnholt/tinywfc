#include <tinywfc/tinywfc.h>
#include <iostream>
int main() {
    for(uint64_t seed=0;seed<100;++seed) {
        tinywfc::Rules rules{4};
        uint64_t state=seed+1;
        for(auto d:{tinywfc::Direction::PositiveX,tinywfc::Direction::PositiveZ})
            for(uint32_t a=0;a<4;++a) for(uint32_t b=0;b<4;++b) {
                state=state*6364136223846793005ull+1;
                if((state>>62)!=0) rules.allow(a,d,b);
            }
        auto solver=tinywfc::Solver::create(rules,{._size={4,1,4},._seed=seed,._max_backtracks=100,._max_restarts=3});
        const auto ok=solver->run().has_value();
        std::cout << seed << ' ' << ok;
        if(ok) {
            const auto stats=solver->statistics();
            std::cout << ' ' << stats._decisions << ' ' << stats._backtracks << ' ' << stats._restarts;
            for(auto tile:solver->result()) std::cout << ' ' << tile;
        }
        std::cout << '\n';
    }
}
