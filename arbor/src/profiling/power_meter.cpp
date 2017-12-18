#include <string>
#include <vector>

#include "meter.hpp"

#include <util/config.hpp>
#include <hardware/power.hpp>

namespace arb {
namespace util {

class power_meter: public meter {
    std::vector<hw::energy_size_type> readings_;

public:
    std::string name() override {
        return "energy";
    }

    std::string units() override {
        return "J";
    }

    std::vector<double> measurements() override {
        std::vector<double> diffs;

        for (auto i=1ul; i<readings_.size(); ++i) {
            diffs.push_back(readings_[i]-readings_[i-1]);
        }

        return diffs;
    }

    void take_reading() override {
        readings_.push_back(hw::energy());
    }
};

meter_ptr make_power_meter() {
    if (not config::has_power_measurement) {
        return nullptr;
    }
    return meter_ptr(new power_meter());
}

} // namespace util
} // namespace arb
