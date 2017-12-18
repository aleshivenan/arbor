#include <communication/global_policy.hpp>
#include <domain_decomposition.hpp>
#include <hardware/node_info.hpp>
#include <recipe.hpp>

namespace arb {

domain_decomposition partition_load_balance(const recipe& rec, hw::node_info nd) {
    struct partition_gid_domain {
        partition_gid_domain(std::vector<cell_gid_type> divs):
            gid_divisions(std::move(divs))
        {}

        int operator()(cell_gid_type gid) const {
            auto gid_part = util::partition_view(gid_divisions);
            return gid_part.index(gid);
        }

        const std::vector<cell_gid_type> gid_divisions;
    };

    using kind_type = std::underlying_type<cell_kind>::type;
    using util::make_span;

    unsigned num_domains = communication::global_policy::size();
    unsigned domain_id = communication::global_policy::id();
    auto num_global_cells = rec.num_cells();

    auto dom_size = [&](unsigned dom) -> cell_gid_type {
        const cell_gid_type B = num_global_cells/num_domains;
        const cell_gid_type R = num_global_cells - num_domains*B;
        return B + (dom<R);
    };

    // Global load balance

    std::vector<cell_gid_type> gid_divisions;
    auto gid_part = make_partition(
        gid_divisions, transform_view(make_span(0, num_domains), dom_size));

    // Local load balance

    std::unordered_map<kind_type, std::vector<cell_gid_type>> kind_lists;
    for (auto gid: make_span(gid_part[domain_id])) {
        kind_lists[rec.get_cell_kind(gid)].push_back(gid);
    }

    // Create a flat vector of the cell kinds present on this node,
    // partitioned such that kinds for which GPU implementation are
    // listed before the others. This is a very primitive attempt at
    // scheduling; the cell_groups that run on the GPU will be executed
    // before other cell_groups, which is likely to be more efficient.
    //
    // TODO: This creates an dependency between the load balancer and
    // the threading internals. We need support for setting the priority
    // of cell group updates according to rules such as the back end on
    // which the cell group is running.
    std::vector<cell_kind> kinds;
    for (auto l: kind_lists) {
        kinds.push_back(cell_kind(l.first));
    }
    std::partition(kinds.begin(), kinds.end(), has_gpu_backend);

    std::vector<group_description> groups;
    for (auto k: kinds) {
        // put all cells into a single cell group on the gpu if possible
        if (nd.num_gpus && has_gpu_backend(k)) {
            groups.push_back({k, std::move(kind_lists[k]), backend_kind::gpu});
        }
        // otherwise place into cell groups of size 1 on the cpu cores
        else {
            for (auto gid: kind_lists[k]) {
                groups.push_back({k, {gid}, backend_kind::multicore});
            }
        }
    }

    // calculate the number of local cells
    auto rng = gid_part[domain_id];
    cell_size_type num_local_cells = rng.second - rng.first;

    domain_decomposition d;
    d.num_domains = num_domains;
    d.domain_id = domain_id;
    d.num_local_cells = num_local_cells;
    d.num_global_cells = num_global_cells;
    d.groups = std::move(groups);
    d.gid_domain = partition_gid_domain(std::move(gid_divisions));

    return d;

    //return domain_decomposition(num_domains, domain_id, num_local_cells, num_global_cells, std::move(groups));
}

} // namespace arb

