#include "modules/lcd/dashboard.hpp"

// Network/Bluetooth operation does not require a physical display or GPIO map.
void lcd_main() {}
void dashboard_init() {}
void dashboard_update(const application::State &) {}
void dashboard_poll() {}
void dashboard_request(int, bool) {}
bool dashboard_control(const std::string &) {return false;}
application::Value dashboard_status() { return application::Value::Object{{"headless",true}}; }
application::Value dashboard_frame(size_t) { return {}; }
