// Vehicle relations only. Instrument identity is never changed by a move.
function assignSensorToWheel(vehicle, sensorId, axleIndex, wheelIndex) {
  const target = vehicle?.axles[axleIndex]?.wheels[wheelIndex];
  if (!sensorId || !target || target.sensor === sensorId) return false;
  let source;
  for (const axle of vehicle.axles)
    for (const wheel of axle.wheels)
      if (wheel.sensor === sensorId) source = wheel;
  const displaced = target.sensor;
  target.sensor = sensorId;
  if (source) source.sensor = displaced;
  return true;
}
