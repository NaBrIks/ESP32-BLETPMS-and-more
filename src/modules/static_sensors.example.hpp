// Copy to static_sensors_secret.hpp in this directory and enter sticker codes
// as hexadecimal values. The private header is ignored by Git.
inline constexpr const char *sensor_set = "SUMMER";
inline constexpr std::array<ConfiguredSensor, 4> configured_sensors{{
    {0, "Front left", "FL"},
    {0, "Front right", "FR"},
    {0, "Rear left", "RL"},
    {0, "Rear right", "RR"}
}};
