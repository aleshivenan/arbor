#include "../gtest.h"

#include <backends/multicore/fvm.hpp>
#include <common_types.hpp>
#include <epoch.hpp>
#include <fvm_multicell.hpp>
#include <mc_cell_group.hpp>
#include <util/rangeutil.hpp>

#include "common.hpp"
#include "../common_cells.hpp"
#include "../simple_recipes.hpp"

using namespace arb;
using fvm_cell = fvm::fvm_multicell<arb::multicore::backend>;

cell make_cell() {
    auto c = make_cell_ball_and_stick();

    c.add_detector({0, 0}, 0);
    c.segment(1)->set_compartments(101);

    return c;
}

TEST(mc_cell_group, get_kind) {
    mc_cell_group<fvm_cell> group{{0}, cable1d_recipe(make_cell()) };

    // we are generating a mc_cell_group which should be of the correct type
    EXPECT_EQ(cell_kind::cable1d_neuron, group.get_cell_kind());
}

TEST(mc_cell_group, test) {
    mc_cell_group<fvm_cell> group{{0}, cable1d_recipe(make_cell()) };

    group.advance(epoch(0, 50), 0.01, {});

    // the model is expected to generate 4 spikes as a result of the
    // fixed stimulus over 50 ms
    EXPECT_EQ(4u, group.spikes().size());
}

TEST(mc_cell_group, sources) {
    // Make twenty cells, with an extra detector on gids 0, 3 and 17
    // to make things more interesting.
    std::vector<cell> cells;

    for (int i=0; i<20; ++i) {
        cells.push_back(make_cell());
        if (i==0 || i==3 || i==17) {
            cells.back().add_detector({1, 0.3}, 2.3);
        }

        EXPECT_EQ(1u + (i==0 || i==3 || i==17), cells.back().detectors().size());
    }

    std::vector<cell_gid_type> gids = {3u, 4u, 10u, 16u, 17u, 18u};
    mc_cell_group<fvm_cell> group{gids, cable1d_recipe(cells)};

    // Expect group sources to be lexicographically sorted by source id
    // with gids in cell group's range and indices starting from zero.

    const auto& sources = group.spike_sources();
    for (unsigned j = 0; j<sources.size(); ++j) {
        auto id = sources[j];
        if (j==0) {
            EXPECT_EQ(id.gid, gids[0]);
            EXPECT_EQ(id.index, 0u);
        }
        else {
            auto prev = sources[j-1];
            EXPECT_GT(id, prev);
            EXPECT_EQ(id.index, id.gid==prev.gid? prev.index+1: 0u);
        }
    }
}
