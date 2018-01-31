#include "gtest/gtest.h"

#include "thorin/analyses/scope.h"
#include "thorin/core/world.h"
#include "thorin/frontend/parser.h"

namespace thorin::core {

TEST(Cn, Simpel) {
    World w;
    auto C = w.cn_type(w.unit());
    auto k = w.cn(parse(w, "cn[int {32s64: nat}, cn int {32s64: nat}]")->as<CnType>(), {"k"});
    auto x = k->param(0, {"x"});
    auto r = k->param(1, {"r"});
    auto t = w.cn(C, {"t"});
    auto f = w.cn(C, {"f"});
    auto n = w.cn(parse(w, "cn[int {32s64: nat}]")->as<CnType>());
    auto i0 = w.lit_i(0_u32);
    auto cmp = w.op<ICmp::ugt>(x, i0);
    k->br(cmp, t, f);
    t->jump(n, w.lit_i(23_u32));
    f->jump(n, w.lit_i(42_u32));
    n->jump(r, n->param());

    w.make_external(k);

    Scope scope(k);
    ASSERT_TRUE(scope.contains(k));
    ASSERT_TRUE(scope.contains(t));
    ASSERT_TRUE(scope.contains(f));
    ASSERT_TRUE(scope.contains(n));
    ASSERT_TRUE(scope.contains(x));
    ASSERT_TRUE(scope.contains(r));
    ASSERT_TRUE(scope.contains(cmp));
    ASSERT_FALSE(scope.contains(i0));
}

}