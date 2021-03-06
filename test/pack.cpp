#include "gtest/gtest.h"

#include "thorin/world.h"

using namespace thorin;

TEST(Pack, Multi) {
    World w;
    auto N = w.type_nat();
    auto n16 = w.lit_nat_16();

    auto p = w.pack({3, 8, 5}, n16);
    EXPECT_EQ(w.pack(1, n16), n16);
    EXPECT_EQ(p, w.pack(3, w.pack(8, w.pack(5, n16))));
    EXPECT_EQ(p->type(), w.variadic({3, 8, 5}, N));
    EXPECT_EQ(w.pack(1, n16), n16);
    EXPECT_EQ(w.pack({3, 1, 4}, n16), w.pack({3, 4}, n16));
    EXPECT_EQ(w.extract(w.extract(w.extract(p, 1), 2), 3), n16);
}

TEST(Pack, Nested) {
    World w;
    auto A = w.kind_arity();
    auto N = w.type_nat();

    EXPECT_EQ(w.pack(w.sigma({w.lit_arity(3), w.lit_arity(2)}), w.var(N, 1)),
              w.pack(3, w.pack(2, w.var(N, 2))));
    EXPECT_EQ(w.pack(w.variadic(w.lit_arity(3), w.lit_arity(2)), w.var(N, 1)),
              w.pack(2, w.pack(2, w.pack(2, w.var(N, 3)))));
    EXPECT_EQ(w.pack(w.variadic(3, w.var(A, 1)), N),
              w.pack(w.var(A, 0), w.pack(w.var(A, 1), w.pack(w.var(A, 2), N))));

    auto p = w.pack(w.variadic(3, w.lit_arity(2)), w.var(w.variadic(3, w.lit_arity(2)), 0));
    const Def* outer_tuples[2];
    for (int z = 0; z != 2; ++z) {
        const Def* inner_tuples[2];
        for (int y = 0; y != 2; ++y) {
            const Def* t[2];
            for (int x = 0; x != 2; ++x)
                t[x] = w.tuple({w.lit_index(2, z), w.lit_index(2, y), w.lit_index(2, x)});
            inner_tuples[y] = w.tuple(t);
        }
        outer_tuples[z] = w.tuple(inner_tuples);
    }
    EXPECT_EQ(p, w.tuple(outer_tuples));
}
