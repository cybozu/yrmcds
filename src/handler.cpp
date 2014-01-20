// (C) 2014 Cybozu.

#include "handler.hpp"

namespace yrmcds {

protocol_handler::~protocol_handler() {}
void protocol_handler::on_start() {}
void protocol_handler::on_master_start() {}
void protocol_handler::on_master_pre_sync() {}
void protocol_handler::on_master_interval() {}
void protocol_handler::on_master_end() {}
void protocol_handler::on_slave_start(int) {}
void protocol_handler::on_slave_interval() {}
void protocol_handler::on_slave_end() {}
void protocol_handler::on_clear() {}
bool protocol_handler::reactor_gc_ready() { return true; }

} // namespace yrmcds
