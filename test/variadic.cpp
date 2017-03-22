#include "gtest/gtest.h"

#include "thorin/world.h"
#include "thorin/primop.h"

using namespace thorin;

TEST(Variadic, Misc) {
    World w;
    auto B = w.type_bool();
    auto N = w.type_nat();
    auto p2_4 = w.index(2, 4);
    auto p2_4b = w.index(2, 4);
    ASSERT_EQ(p2_4, p2_4b);
    auto v = w.variadic(5, N);
    ASSERT_TRUE(w.dim(v) == w.arity(5));
    ASSERT_TRUE(is_array(v));

    auto t = w.tuple({w.val_nat_2(), w.val_nat_4()});
    ASSERT_TRUE(t->type()->isa<Variadic>());

    // ΠT:*,a:N.Π(ptr[T,a], i:dim(T)).ptr[T.i,a]

    auto s2 = w.sigma({B, N});
    auto n2 = w.sigma_type(2)->set(0, B)->set(1, N);
    auto ps2 = w.axiom(w.type_ptr(s2, w.val_nat_2()), {"ptr_s2"});
    auto pn2 = w.axiom(w.type_ptr(n2, w.val_nat_4()), {"ptr_n2"});

    auto lea1 = LEA(w.op_lea(ps2, 1));
    auto lea2 = LEA(w.op_lea(pn2, 1));
    ASSERT_EQ(lea1.type(), w.type_ptr(N, w.val_nat_2()));
    ASSERT_EQ(lea2.type(), w.type_ptr(N, w.val_nat_4()));
    ASSERT_EQ(lea1.ptr_pointee(), s2);
    ASSERT_EQ(lea2.ptr_pointee(), n2);
    ASSERT_EQ(w.op_insert(w.tuple({w.val_bool_top(), w.val_nat_2()}), 1, w.val_nat_8())->type(), s2);

    auto list = w.axiom(w.pi(w.star(), w.star()), {"list"});
    auto lb = w.axiom(w.app(list, B), {"lb"});
    auto ln = w.axiom(w.app(list, N ), {"ln"});

    // ΠT:*.Π(Vi:dim(T).list[T.i],list[T])
    auto zip = w.axiom(w.pi(w.star(),
                            w.pi(w.variadic(w.dim(w.var(w.star(), 0)), w.app(list, w.extract(w.var(w.star(), 1), w.var(w.dim(w.var(w.star(), 1)), 0)))),
                                 w.app(list, w.var(w.star(), 1)))), {"zip"});
    ASSERT_EQ(w.app(w.app(zip, s2), {lb, ln})->type(), w.app(list, s2));
    ASSERT_EQ(w.app(w.app(zip, B), lb)->type(), w.app(list, B));

    // ΠT:*.Π(list[T],Vi:dim(T).list[T.i])
    auto rip = w.axiom(w.pi(w.star(),
                            w.pi(w.app(list, w.var(w.star(), 0)),
                                 w.variadic(w.dim(w.var(w.star(), 1)), w.app(list, w.extract(w.var(w.star(), 2), w.var(w.dim(w.var(w.star(), 2)), 0)))))), {"rip"});
    auto l = w.axiom(w.app(list, s2), {"l"});
    ASSERT_EQ(w.app(w.app(rip, s2), l)->type(), w.sigma({lb->type(), ln->type()}));

    ASSERT_EQ(w.pi(w.variadic(3, N), B), w.pi({N, N, N}, B));
}

TEST(Variadic, Multi) {
    World w;
    auto N = w.type_nat();

    ASSERT_EQ(w.variadic({3, 8, 5}, N), w.variadic(3, w.variadic(8, w.variadic(5, N))));
    auto e1 = w.extract(w.var(w.variadic({3, 8, 5}, N), 0), 2);
    auto e2 = w.extract(e1, 2);
    auto e3 = w.extract(e2, 3);
    auto f1 = w.var(w.variadic({8, 5}, N), 0);
    auto f2 = w.extract(f1, 2);
    auto f3 = w.extract(f2, 3);
    ASSERT_EQ(w.lambda(w.variadic(3, w.variadic(8, w.variadic(5, N))), e3),
              w.lambda({w.variadic({8, 5}, N), w.variadic({8, 5}, N), w.variadic({8, 5}, N)}, f3));

    auto A = w.arity_kind();
    auto arity_tuple = [&](auto i) { return w.variadic(w.var(A, i), A); };
    auto a = [&](auto i) { return w.var(A, i); };
    auto build_variadic = w.variadic(a(1), w.extract(w.var(arity_tuple(2), 1), w.var(a(2), 0)));
    ASSERT_TRUE(build_variadic->free_vars().test(0));
    ASSERT_TRUE(build_variadic->free_vars().test(1));
    ASSERT_TRUE(build_variadic->free_vars().none_begin(2));
    auto arity_tuple_to_type = w.lambda({/*a:*/A, arity_tuple(0)}, build_variadic);
    ASSERT_TRUE(arity_tuple_to_type->free_vars().none());
    ASSERT_TRUE(arity_tuple_to_type->type()->free_vars().none());

    auto args = w.tuple({w.arity(2), w.tuple({w.arity(3), w.arity(4)})});
    ASSERT_EQ(w.app(arity_tuple_to_type, args), w.sigma({w.arity(3), w.arity(4)}));

    auto l = w.lambda({/*a:*/A, arity_tuple(0)},
                      w.variadic(w.variadic(a(1), w.extract(w.var(arity_tuple(2), 1), w.var(a(2), 0))), N));
    ASSERT_EQ(w.app(l, args), w.variadic({3, 4}, N));
}

TEST(Variadic, InlineSigmaInterOp) {
    World w;
    auto star = w.star();
    auto pair = w.sigma({star, star});
    ASSERT_TRUE(pair->isa<Variadic>());

    auto lam = w.lambda(pair, w.type_nat(), {"lam"});
    auto app = w.app(lam, w.var(pair, 1));
    ASSERT_EQ(app, w.type_nat());
}
