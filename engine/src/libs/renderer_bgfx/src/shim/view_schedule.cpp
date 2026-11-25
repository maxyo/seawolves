#include "view_schedule.hpp"

#include "shim.hpp"

#include <algorithm>

namespace storm::shim
{

void ViewSchedule::begin()
{
    for (View &view : views_)
    {
        view.opened = false;
        view.writes.clear();
        view.reads.clear();
        view.clear = {};
        view.twin = view.twin_of = UINT16_MAX;
    }
}

ViewSchedule::View &ViewSchedule::at(uint16_t view)
{
    const size_t index = static_cast<size_t>(view - first_);
    if (index >= views_.size())
        views_.resize(index + 1);
    return views_[index];
}

void ViewSchedule::open(uint16_t view, Lane lane, const char *name, std::initializer_list<uint16_t> writes)
{
    View &v = at(view);
    v.opened = true;
    v.lane = lane;
    v.name = name;
    v.writes.assign(writes.begin(), writes.end());
    v.reads.clear();
    v.clear = {};
    v.twin = v.twin_of = UINT16_MAX;
}

void ViewSchedule::read(uint16_t view, uint16_t texture)
{
    View &v = at(view);
    if (std::find(v.reads.begin(), v.reads.end(), texture) == v.reads.end())
        v.reads.push_back(texture);
}

void ViewSchedule::write(uint16_t view, uint16_t texture)
{
    View &v = at(view);
    if (std::find(v.writes.begin(), v.writes.end(), texture) == v.writes.end())
        v.writes.push_back(texture);
}

void ViewSchedule::twin(uint16_t before, uint16_t view)
{
    at(before).twin_of = view;
    at(view).twin = before;
}

void ViewSchedule::set_clear(uint16_t view, const Clear &clear)
{
    at(view).clear = clear;
    at(view).clear.known = true;
}

bool ViewSchedule::conflict(const View &earlier, const View &later)
{
    // A view nothing was recorded for (the budget ran out) is a barrier: nothing moves across it.
    if (!earlier.opened || !later.opened)
        return true;
    const auto meets = [](const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
        for (const uint16_t x : a)
            if (std::find(b.begin(), b.end(), x) != b.end())
                return true;
        return false;
    };
    return meets(earlier.writes, later.reads) || meets(earlier.writes, later.writes) ||
           meets(earlier.reads, later.writes);
}

void ViewSchedule::plan(uint16_t count, std::vector<uint16_t> &order)
{
    if (views_.size() < count)
        views_.resize(count);
    const auto lane = [&](size_t i) { return views_[i].opened ? views_[i].lane : Lane::Picture; };
    // A list schedule: a view is ready when the earlier views it conflicts with ran; of the ready views the one of the
    // earliest lane runs, ties in submission order. Every dependency points to an earlier view: no circle.
    // A twin is placed right before its view, which holds its dependencies (the same targets): it takes no part. A view
    // opened again after it got a twin has none.
    const auto placed_alone = [&](size_t i) {
        const uint16_t of = views_[i].twin_of;
        return of == UINT16_MAX || of - first_ >= static_cast<int>(count) || views_[of - first_].twin != first_ + i;
    };
    std::vector<std::vector<size_t>> after(count); // after[u]: the views that wait for u
    std::vector<uint32_t> waiting(count, 0);
    for (size_t v = 0; v < count; ++v)
        for (size_t u = 0; u < v; ++u)
            if (placed_alone(u) && placed_alone(v) && conflict(views_[u], views_[v]))
            {
                after[u].push_back(v);
                ++waiting[v];
            }
    std::vector<bool> done(count, false);
    for (size_t i = 0; i < count; ++i)
        done[i] = !placed_alone(i);
    order.clear();
    while (order.size() < count)
    {
        size_t next = SIZE_MAX;
        for (size_t i = 0; i < count; ++i)
            if (!done[i] && waiting[i] == 0 && (next == SIZE_MAX || lane(i) < lane(next)))
                next = i;
        done[next] = true;
        if (const uint16_t twin = views_[next].twin; twin != UINT16_MAX && !placed_alone(twin - first_))
            order.push_back(twin);
        order.push_back(static_cast<uint16_t>(first_ + next));
        for (const size_t v : after[next])
            --waiting[v];
    }
    scene_views_ = scene_passes_ = 0;
    bool in_scene = false;
    for (size_t k = 0; k < order.size(); ++k)
    {
        const size_t i = order[k] - first_;
        const bool scene = lane(i) == Lane::Scene;
        scene_views_ += scene;
        if (scene && !in_scene && scene_passes_ > 0)
        {
            // A view of another lane cut the scene: name it with the scene view it had to follow (logged once per
            // pair).
            for (size_t j = k; j-- > 0;)
            {
                const size_t cut = order[j] - first_;
                if (lane(cut) == Lane::Scene)
                    break;
                for (size_t u = 0; u < cut; ++u)
                    if (lane(u) == Lane::Scene && conflict(views_[u], views_[cut]))
                    {
                        std::string pair = views_[cut].name + " after " + views_[u].name;
                        if (std::find(logged_.begin(), logged_.end(), pair) == logged_.end())
                        {
                            render::Log().info(
                                "view order: '{}' cuts the scene: it runs after '{}' (it reads what that view drew, or "
                                "that view read or drew its target before) (logged once)",
                                views_[cut].name, views_[u].name);
                            logged_.push_back(std::move(pair));
                        }
                        break;
                    }
            }
        }
        scene_passes_ += scene && !in_scene;
        in_scene = scene;
    }
    order_ = order;
}

uint16_t ViewSchedule::last_writer(uint16_t texture) const
{
    for (auto it = order_.rbegin(); it != order_.rend(); ++it)
    {
        const View &view = views_[*it - first_];
        if (view.opened && std::find(view.writes.begin(), view.writes.end(), texture) != view.writes.end())
            return *it;
    }
    return UINT16_MAX;
}

} // namespace storm::shim
