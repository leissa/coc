#ifndef THORIN_WORLD_H
#define THORIN_WORLD_H

#include "thorin/def.h"

namespace thorin {

class World {
public:
    struct DefHash { uint64_t operator()(const Def* def) const { return def->hash(); } };
    struct DefEqual { bool operator()(const Def* d1, const Def* d2) const { return d2->equal(d1); } };
    typedef HashSet<const Def*, DefHash, DefEqual> DefSet;

    World& operator=(const World&);
    World(const World&);

    World();
    virtual ~World() { for (auto def : defs_) delete def; }

    const Star* star() const { return star_; }
    const Var* var(const Def* type, int depth, const std::string& name = "") { return unify(new Var(*this, type, depth, name)); }
    const Assume* assume(const Def* type, const std::string& name = "") { return insert(new Assume(*this, type, name)); }
    const Lambda* lambda(const Def* domain, const Def* body, const std::string& name = "") { return unify(new Lambda(*this, domain, body, name)); }
    const Pi*     pi    (const Def* domain, const Def* body, const std::string& name = "") { return unify(new Pi    (*this, domain, body, name)); }
    const Def* app(const Def* callee, Defs args, const std::string& name = "");
    const Def* app(const Def* callee, const Def* arg, const std::string& name = "") { return app(callee, Defs({arg}), name); }
    const Def* tuple(const Def* type, Defs ops, const std::string& name = "");
    const Def* tuple(Defs defs, const std::string& name = "") { return tuple(sigma(types(defs), name), defs, name); }
    const Sigma* sigma(Defs ops, const std::string& name = "") { return unify(new Sigma(*this, ops, name)); }
    Sigma* sigma(size_t num_ops, const std::string& name = "") { return insert(new Sigma(*this, num_ops, name)); }
    const Sigma* unit() { return sigma(Defs()); }
    const Assume* nat() { return nat_; }

    const DefSet& defs() const { return defs_; }

    friend void swap(World& w1, World& w2) {
        using std::swap;
        swap(w1.defs_, w2.defs_);
        w1.fix();
        w2.fix();
    }

private:
    void fix() {
        for (auto def : defs_)
            def->world_ = this;
    }

protected:
    const Def* unify_base(const Def* type);
    template<class T>
    const T* unify(const T* type) { return unify_base(type)->template as<T>(); }

    template<class T>
    T* insert(T* def) {
        auto p = defs_.emplace(def);
        assert_unused(p.second);
        return def;
    }

    DefSet defs_;
    const Star* star_;
    const Assume* nat_;
};

}

#endif
