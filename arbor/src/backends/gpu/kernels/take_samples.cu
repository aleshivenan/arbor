#include <common_types.hpp>
#include <backends/event.hpp>
#include <backends/fvm_types.hpp>
#include <backends/multi_event_stream_state.hpp>

namespace arb {
namespace gpu {

namespace kernels {
    __global__ void take_samples(
	    multi_event_stream_state<raw_probe_info> s,
	    const fvm_value_type* time,
	    fvm_value_type* sample_time,
	    fvm_value_type* sample_value)
    {
        int i = threadIdx.x+blockIdx.x*blockDim.x;

        if (i<s.n) {
            auto begin = s.ev_data+s.begin_offset[i];
            auto end = s.ev_data+s.end_offset[i];
            for (auto p = begin; p!=end; ++p) {
                sample_time[p->offset] = time[i];
                sample_value[p->offset] = *p->handle;
            }
        }
    }
}

void take_samples(
	const multi_event_stream_state<raw_probe_info>& s,
	const fvm_value_type* time,
	fvm_value_type* sample_time,
	fvm_value_type* sample_value)
{
    if (!s.n_streams()) {
        return;
    }

    constexpr int blockwidth = 128;
    int nblock = 1+(s.n_streams()-1)/blockwidth;
    kernels::take_samples<<<nblock, blockwidth>>>(s, time, sample_time, sample_value);
}

} // namespace gpu
} // namespace arb

