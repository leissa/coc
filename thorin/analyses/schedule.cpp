#include "thorin/analyses/schedule.h"

#include <fstream>

#include "thorin/world.h"
#include "thorin/analyses/cfg.h"
#include "thorin/analyses/domtree.h"
#include "thorin/analyses/looptree.h"
#include "thorin/analyses/scope.h"
#include "thorin/util/log.h"

namespace thorin {

typedef DefMap<const CFNode*> Def2CFNode;

//------------------------------------------------------------------------------

class Scheduler {
public:
    Scheduler(const Scope& scope, Schedule& schedule)
        : scope_(scope)
        , cfg_(scope.f_cfg())
        , domtree_(cfg_.domtree())
        , looptree_(cfg_.looptree())
        , schedule_(schedule)
    {
        compute_def2uses();
        Def2CFNode* def2node = nullptr;

        switch (schedule.tag()) {
            case Schedule::Early: schedule_early(); def2node = &def2early_; break;
            case Schedule::Late:  schedule_late();  def2node = &def2late_;  break;
            case Schedule::Smart: schedule_smart(); def2node = &def2smart_; break;
        }

        for (const auto& p : *def2node)
            schedule[p.second].defs_.push_back(p.first);

        topo_sort(*def2node);
    }

    void for_all_defs(std::function<void(const Def*)> f) {
        for (const auto& p : def2uses_) {
            f(p.first);
        }
    }

    const Uses& uses(const Def* def) const { return def2uses_.find(def)->second; }
    void compute_def2uses();
    void schedule_early() { for_all_defs([&](const Def* def) { schedule_early(def); }); }
    void schedule_late()  { for_all_defs([&](const Def* def) { schedule_late (def); }); }
    void schedule_smart() { for_all_defs([&](const Def* def) { schedule_smart(def); }); }
    const CFNode* schedule_early(const Def*);
    const CFNode* schedule_late(const Def*);
    const CFNode* schedule_smart(const Def*);
    void topo_sort(Def2CFNode&);

private:
    const Scope& scope_;
    const F_CFG& cfg_;
    const DomTree& domtree_;
    const LoopTree<true>& looptree_;
    DefMap<Uses> def2uses_;
    Def2CFNode def2early_;
    Def2CFNode def2late_;
    Def2CFNode def2smart_;
    Schedule& schedule_;
};

void Scheduler::compute_def2uses() {
    for (auto def : scope_.defs()) {
        auto& uses = def2uses_[def];
        for (auto use : def->uses()) {
            if (scope_.contains(use))
                uses.emplace(use);
        }
    }
}

const CFNode* Scheduler::schedule_early(const Def* def) {
    auto i = def2early_.find(def);
    if (i != def2early_.end())
        return i->second;

    if (auto param = def->isa<Param>()) {
        DLOG("early: {} <- {}", param->lambda(), def);
        return def2early_[def] = cfg_[param->lambda()];
    }

    auto result = cfg_.entry();
    for (auto op : def->ops()) {
        if (!op->isa_lambda() && def2uses_.find(op) != def2uses_.end()) {
            auto n = schedule_early(op);
            if (domtree_.depth(n) > domtree_.depth(result))
                result = n;
        }
    }

    DLOG("early: {} <- {}", result, def);
    return def2early_[def] = result;
}

const CFNode* Scheduler::schedule_late(const Def* def) {
    auto i = def2late_.find(def);
    if (i != def2late_.end())
        return i->second;

    if (auto lambda = def->isa_lambda()) {
        DLOG("late: {} <- {}", lambda, lambda);
        return def2late_[lambda] = cfg_[lambda];
    }

    const CFNode* result = nullptr;
    for (auto use : uses(def)) {
        auto n = schedule_late(use);
        result = result ? domtree_.lca(result, n) : n;
    }

    DLOG("late: {} <- {}", result, def);
    return def2late_[def] = result;
}

const CFNode* Scheduler::schedule_smart(const Def* def) {
    auto i = def2smart_.find(def);
    if (i != def2smart_.end())
        return i->second;

    auto early = schedule_early(def);
    auto late  = schedule_late (def);

    const CFNode* result;
    result = late;
    int depth = looptree_[late]->depth();
    for (auto i = late; i != early;) {
        auto idom = domtree_.idom(i);
        assert(i != idom);
        i = idom;

        // HACK this should actually never occur
        if (i == nullptr) {
            WLOG("don't know where to put {}", def);
            result = late;
            break;
        }

        int cur_depth = looptree_[i]->depth();
        if (cur_depth < depth) {
            result = i;
            depth = cur_depth;
        }
    }

    DLOG("smart: {} <- {}", result, def);
    return def2smart_[def] = result;
}

void Scheduler::topo_sort(Def2CFNode& def2node) {
    for (auto& block : schedule_.blocks_) {
        std::vector<const Def*> defs;
        std::queue<const Def*> queue;
        DefSet done;

        auto inside = [&](const Def* def) {
            auto i = def2node.find(def);
            return i != def2node.end() && i->second == block.node();
        };

        auto enqueue = [&](const Def* def) {
            if (!done.contains(def)) {
                for (auto op : def->ops()) {
                    if (inside(op) && !done.contains(op))
                        return;
                }

                queue.push(def);
                done.emplace(def);
                defs.push_back(def);
            }
        };

        for (auto def : block)
            enqueue(def);

        while (!queue.empty()) {
            auto def = pop(queue);

            for (auto use : uses(def)) {
                if (inside(use))
                    enqueue(use);
            }
        }

        assert(block.defs_.size() == defs.size());
        swap(block.defs_, defs);
    }
}

//------------------------------------------------------------------------------

Schedule::Schedule(const Scope& scope, Tag tag)
    : scope_(scope)
    , indices_(cfg())
    , blocks_(cfa().size())
    , tag_(tag)
{
    block_schedule();
    Scheduler(scope, *this);
    verify();
}

void Schedule::block_schedule() {
    // until we have sth better simply use the RPO of the CFG
    size_t i = 0;
    for (auto n : cfg().reverse_post_order()) {
        auto& block = blocks_[i];
        block.node_ = n;
        block.index_ = i;
        indices_[n] = i++;
    }
    assert(blocks_.size() == i);
}

void Schedule::verify() {
#if 0
#ifndef NDEBUG
    auto& domtree = cfg().domtree();
    Schedule::Map<const Def*> block2mem(*this);

    for (auto& block : *this) {
        const Def* mem = block.lambda()->mem_param();
        auto idom = block.lambda() != scope().entry() ? domtree.idom(block.node()) : block.node();
        mem = mem ? mem : block2mem[(*this)[idom]];
        for (auto def : block) {
            if (auto memop = def->isa<MemOp>()) {
                if (memop->mem() != mem)
                    WLOG(memop, "incorrect schedule: {} (current mem is {}) - scope entry: {}", memop, mem, scope_.entry());
                mem = memop->out_mem();
            }
        }
        block2mem[block] = mem;
    }
#endif
#endif
}

Printer& Schedule::stream(Printer& p) const {
#if 0
    for (auto& block : *this) {
        auto lambda = block.lambda();
        if (lambda != world().lambda_end()) {
            bool indent = lambda != scope().entry();
            if (indent)
                os << up;
            os << endl;
            lambda->stream_head(os) << up_endl;
            for (auto def : block)
                def->stream_assignment(os);

            lambda->stream_jump(os) << down_endl;
            if (indent)
                os << down;
        }
    }
    return os << endl;
#endif
    return p;
}

void Schedule::write_thorin(const char* filename) const {
    std::ofstream file(filename);
    Printer printer(file);
    stream(printer);
}

void Schedule::thorin() const {
    auto filename = world().name() + "_" + scope().entry()->unique_name() + ".thorin";
    write_thorin(filename.c_str());
}

//------------------------------------------------------------------------------

}
