#include "error.hh"

#include <clean-core/format.hh>

cc::string res::error::to_string() const { return cc::format("[%s] %s", type, description); }
