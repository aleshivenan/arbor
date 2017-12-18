#pragma once

#include <backends/fvm_types.hpp>

namespace arb {
namespace gpu {

// stores a single crossing event
struct threshold_crossing {
    fvm_size_type index;    // index of variable
    fvm_value_type time;    // time of crossing

    friend bool operator==(threshold_crossing l, threshold_crossing r) {
        return l.index==r.index && l.time==r.time;
    }
};

// Concrete storage of gpu stack datatype.
// The stack datatype resides in host memory, and holds a pointer to the
// stack_storage in managed memory, which can be accessed by both host and
// gpu code.
template <typename T>
struct stack_storage {
    using value_type = T;

    // The number of items of type value_type that can be stored in the stack
    unsigned capacity;

    // The number of items that have been stored.
    // This may exceed capacity if more stores were attempted than it is
    // possible to store, in which case only the first capacity values are valid.
    unsigned stores;

    // Memory containing the value buffer
    value_type* data;
};


} // namespace gpu
} // namespace arb
