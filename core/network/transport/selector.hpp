#pragma once

#include <core/types.hpp>

namespace smo::network::transport {

// Transport selector — auto-chooses TCP vs UDP based on reachability.
// Priority: direct TCP > UDP hole-punch > relay.
// Full implementation in Sprint 4 (post NAT traversal).

} // namespace smo::network::transport
