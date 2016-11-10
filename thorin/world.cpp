#include "thorin/world.h"

namespace thorin {

World::World()
    : root_page_(new Page)
    , cur_page_(root_page_.get())
    , star_(unify(alloc<Star>(0, *this)))
    , nat_(assume(star(), "Nat"))
    , boolean_(assume(star(), "Boolean"))
{}

const Lambda* World::lambda(Defs domains, const Def* body, const std::string& name) {
    if (domains.size() == 1 && domains.front()->type()) {
        if (auto sigma = domains.front()->type()->isa<Sigma>())
            return lambda(sigma->ops(), body, name);
    }

    auto type = pi(domains, body->type());
    return unify(alloc<Lambda>(domains.size() + 1, *this, type, domains, body, name));
}

const Pi* World::pi(Defs domains, const Def* body, const std::string& name) {
    if (domains.size() == 1 && domains.front()->type()) {
        if (auto sigma = domains.front()->type()->isa<Sigma>())
            return pi(sigma->ops(), body, name);
    }

    return unify(alloc<Pi>(domains.size() + 1, *this, domains, body, name));
}

const Def* World::tuple(const Def* type, Defs defs, const std::string& name) {
    if (defs.size() == 1)
        return defs.front();

    return unify(alloc<Tuple>(defs.size(), *this, type, defs, name));
}

const Def* World::sigma(Defs defs, const std::string& name) {
    if (defs.size() == 1)
        return defs.front();

    return unify(alloc<Sigma>(defs.size(), *this, defs, name));
}

const Def* World::cup(Defs defs, const std::string& name) {
    return unify(alloc<Cup>(defs.size(), *this, defs, name));
}

const Def* World::wedge(Defs defs, const std::string& name) {
    return unify(alloc<Wedge>(defs.size(), *this, /*TODO*/nullptr, defs, name));
}

const Def* World::cap(Defs defs, const std::string& name) {
    return unify(alloc<Cap>(defs.size(), *this, defs, name));
}

const Def* World::vee(const Def* type, const Def* def, const std::string& name) {
    return unify(alloc<Vee>(1, *this, type, def, name));
}

const Def* World::app(const Def* callee, Defs args, const std::string& name) {
    if (args.size() == 1) {
        if (auto tuple = args.front()->isa<Tuple>())
            return app(callee, tuple->ops(), name);
    }

    auto type = callee->type()->as<Quantifier>()->reduce(args);
    auto app = unify(alloc<App>(args.size() + 1, *this, type, callee, args, name));

    if (auto cache = app->cache_)
        return cache;
    if (auto abs = app->callee()->isa<Abs>())
        return app->cache_ = abs->reduce(args);
    else
        return app->cache_ = app;

    return app;
}

const Def* World::extract(const Def* def, const Def* i) {
    if (!def->isa<Sigma>() && !def->type()->isa<Sigma>()) {
        assert(i->name() == "0");
        return def;
    }

    return app(def, i);
}

}
