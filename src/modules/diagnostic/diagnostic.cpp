#include "diagnostic.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <vector>

static const char *task_state(eTaskState state)
{
    switch(state)
    {
    case eRunning: return "R";
    case eReady: return "Y";
    case eBlocked: return "B";
    case eSuspended: return "S";
    case eDeleted: return "D";
    default: return "?";
    }
}

lvgl_cbor::DataValue task_diagnostics()
{
    using Value = lvgl_cbor::DataValue;
    // SDK fills bounded records atomically. Additional slots tolerate task
    // creation between sizing and capture; a failed capture stays explicit.
    std::vector<TaskStatus_t> records(uxTaskGetNumberOfTasks() + 8);
    configRUN_TIME_COUNTER_TYPE total{};
    const auto count = uxTaskGetSystemState(records.data(), records.size(), &total);
    Value::Array tasks;
    for(UBaseType_t index = 0; index < count; ++index)
    {
        const auto &task = records[index];
        tasks.emplace_back(Value::Object{
            {"name", task.pcTaskName}, {"s", task_state(task.eCurrentState)},
            {"p", task.uxCurrentPriority}, {"sf", task.usStackHighWaterMark},
            {"tn", task.xTaskNumber}, {"rt", task.ulRunTimeCounter},
            {"rp", total ? double(task.ulRunTimeCounter) * 100.0 / double(total) : 0.0}});
    }
    return Value::Object{{"msgType", "diagnostic"}, {"tasks", std::move(tasks)},
        {"total_tasks", count}, {"capture_ok", count != 0},
        {"free_heap", heap_caps_get_free_size(MALLOC_CAP_8BIT)},
        {"internal_free", heap_caps_get_free_size(MALLOC_CAP_INTERNAL)},
        {"internal_largest", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)},
        {"minimum_heap", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)}};
}
