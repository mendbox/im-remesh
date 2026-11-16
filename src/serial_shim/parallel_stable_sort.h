// SPDX-License-Identifier: GPL-3.0-or-later
// Serial stand-in for pss::parallel_stable_sort (the upstream "pss" submodule is not used).
#pragma once
#include <algorithm>

namespace pss {
template <typename Iterator, typename Compare>
void parallel_stable_sort(Iterator begin, Iterator end, Compare comp) {
    std::stable_sort(begin, end, comp);
}
template <typename Iterator> void parallel_stable_sort(Iterator begin, Iterator end) {
    std::stable_sort(begin, end);
}
} // namespace pss
