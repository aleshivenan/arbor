#include <numeric>
#include <vector>

#include "../gtest.h"

#include <math.hpp>
#include <matrix.hpp>
#include <backends/multicore/fvm.hpp>
#include <util/span.hpp>

#include "common.hpp"

using namespace arb;

using matrix_type = matrix<arb::multicore::backend>;
using size_type  = matrix_type::size_type;
using value_type = matrix_type::value_type;

using vvec = std::vector<value_type>;

TEST(matrix, construct_from_parent_only)
{
    std::vector<size_type> p = {0,0,1};
    matrix_type m(p, {0, 3}, vvec(3), vvec(3), vvec(3));
    EXPECT_EQ(m.num_cells(), 1u);
    EXPECT_EQ(m.size(), 3u);
    EXPECT_EQ(p.size(), 3u);

    auto mp = m.p();
    EXPECT_EQ(mp[0], 0u);
    EXPECT_EQ(mp[1], 0u);
    EXPECT_EQ(mp[2], 1u);
}

TEST(matrix, solve_host)
{
    using util::make_span;
    using memory::fill;

    // trivial case : 1x1 matrix
    {
        matrix_type m({0}, {0,1}, vvec(1), vvec(1), vvec(1));
        auto& state = m.state_;
        fill(state.d,  2);
        fill(state.u, -1);
        fill(state.rhs,1);

        m.solve();

        EXPECT_EQ(m.solution()[0], 0.5);
    }

    // matrices in the range of 2x2 to 1000x1000
    {
        for(auto n : make_span(2u,1001u)) {
            auto p = std::vector<size_type>(n);
            std::iota(p.begin()+1, p.end(), 0);
            matrix_type m(p, {0, n}, vvec(n), vvec(n), vvec(n));

            EXPECT_EQ(m.size(), n);
            EXPECT_EQ(m.num_cells(), 1u);

            auto& A = m.state_;

            fill(A.d,  2);
            fill(A.u, -1);
            fill(A.rhs,1);

            m.solve();

            auto x = m.solution();
            auto err = math::square(std::fabs(2.*x[0] - x[1] - 1.));
            for(auto i : make_span(1,n-1)) {
                err += math::square(std::fabs(2.*x[i] - x[i-1] - x[i+1] - 1.));
            }
            err += math::square(std::fabs(2.*x[n-1] - x[n-2] - 1.));

            EXPECT_NEAR(0., std::sqrt(err), 1e-8);
        }
    }
}

TEST(matrix, zero_diagonal)
{
    // Combined matrix may have zero-blocks, corresponding to a zero dt.
    // Zero-blocks are indicated by zero value in the diagonal (the off-diagonal
    // elements should be ignored).
    // These submatrices should leave the rhs as-is when solved.

    using memory::make_const_view;

    // Three matrices, sizes 3, 3 and 2, with no branching.
    std::vector<size_type> p = {0, 0, 1, 3, 3, 5, 5};
    std::vector<size_type> c = {0, 3, 5, 7};
    matrix_type m(p, c, vvec(7), vvec(7), vvec(7));

    EXPECT_EQ(7u, m.size());
    EXPECT_EQ(3u, m.num_cells());

    auto& A = m.state_;
    A.d =   make_const_view(vvec({2,  3,  2, 0,  0,  4,  5}));
    A.u =   make_const_view(vvec({0, -1, -1, 0, -1,  0, -2}));
    A.rhs = make_const_view(vvec({3,  5,  7, 7,  8, 16, 32}));

    // Expected solution:
    std::vector<value_type> expected = {4, 5, 6, 7, 8, 9, 10};

    m.solve();
    auto x = m.solution();

    EXPECT_TRUE(testing::seq_almost_eq<double>(expected, x));
}

TEST(matrix, zero_diagonal_assembled)
{
    // Use assemble method to construct same zero-diagonal
    // test case from CV data.

    using util::assign;
    using memory::make_view;

    // Combined matrix may have zero-blocks, corresponding to a zero dt.
    // Zero-blocks are indicated by zero value in the diagonal (the off-diagonal
    // elements should be ignored).
    // These submatrices should leave the rhs as-is when solved.

    // Three matrices, sizes 3, 3 and 2, with no branching.
    std::vector<size_type> p = {0, 0, 1, 3, 3, 5, 5};
    std::vector<size_type> c = {0, 3, 5, 7};

    // Face conductances.
    vvec g = {0, 1, 1, 0, 1, 0, 2};

    // dt of 1e-3.
    vvec dt(3, 1.0e-3);

    // Capacitances.
    vvec Cm = {1, 1, 1, 1, 1, 2, 3};

    // Intial voltage of zero; currents alone determine rhs.
    vvec v(7, 0.0);
    vvec area(7, 1.0);
    vvec i = {-3000, -5000, -7000, -6000, -9000, -16000, -32000};

    // Expected matrix and rhs:
    // u   = [ 0 -1 -1  0 -1  0 -2]
    // d   = [ 2  3  2  2  2  4  5]
    // rhs = [ 3  5  7  2  4 16 32]
    //
    // Expected solution:
    // x = [ 4  5  6  7  8  9 10]

    matrix_type m(p, c, Cm, g, area);
    m.assemble(make_view(dt), make_view(v), make_view(i));
    m.solve();

    vvec x;
    assign(x, on_host(m.solution()));
    std::vector<value_type> expected = {4, 5, 6, 7, 8, 9, 10};

    EXPECT_TRUE(testing::seq_almost_eq<double>(expected, x));

    // Set dt of 2nd (middle) submatrix to zero. Solution
    // should then return voltage values for that submatrix.

    dt[1] = 0;
    v[3] = 20;
    v[4] = 30;
    m.assemble(make_view(dt), make_view(v), make_view(i));
    m.solve();

    assign(x, m.solution());
    expected = {4, 5, 6, 20, 30, 9, 10};

    EXPECT_TRUE(testing::seq_almost_eq<double>(expected, x));
}

