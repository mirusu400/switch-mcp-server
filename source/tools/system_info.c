#include "system_info.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <switch.h>

static cJSON *build_system_status(void) {
    cJSON *status = cJSON_CreateObject();
    size_t heap_free = 0;
    time_t now = time(NULL);
    char utc_time[32] = {0};
    struct tm tm_utc = {0};

    svcGetInfo(&heap_free, 0, 0, 6);
    if (gmtime_r(&now, &tm_utc)) {
        strftime(utc_time, sizeof(utc_time), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    }

    cJSON_AddStringToObject(status, "platform", "nintendo-switch");
    cJSON_AddStringToObject(status, "runtime", "atmosphere-sysmodule");
    cJSON_AddNumberToObject(status, "system_tick", (double)svcGetSystemTick());
    cJSON_AddNumberToObject(status, "unix_time", (double)now);
    cJSON_AddStringToObject(status, "utc_time", utc_time[0] ? utc_time : "unknown");
    cJSON_AddNumberToObject(status, "heap_free_bytes", (double)heap_free);
    cJSON_AddBoolToObject(status, "sdmc_mounted", access("/switch", F_OK) == 0);
    return status;
}

static int add_json_text_content(cJSON *content, cJSON *json) {
    char *text = cJSON_PrintUnformatted(json);
    cJSON *item = cJSON_CreateObject();
    if (!text || !item) {
        free(text);
        cJSON_Delete(item);
        return 1;
    }

    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(content, item);
    free(text);
    return 0;
}

int list_system_info(cJSON *tools) {
    cJSON *tool = cJSON_CreateObject();
    cJSON *input_schema = cJSON_CreateObject();
    cJSON *properties = cJSON_CreateObject();
    cJSON *required = cJSON_CreateArray();

    cJSON_AddStringToObject(tool, "name", "system_info");
    cJSON_AddStringToObject(tool, "title", "system_info");
    cJSON_AddStringToObject(tool, "description", "inspect generic Switch runtime status for automation and diagnostics");

    cJSON_AddStringToObject(input_schema, "type", "object");
    cJSON_AddItemToObject(input_schema, "properties", properties);
    cJSON_AddItemToObject(input_schema, "required", required);
    cJSON_AddItemToObject(tool, "inputSchema", input_schema);
    cJSON_AddItemToArray(tools, tool);
    return 0;
}

int call_system_info(cJSON *content, const cJSON *arguments) {
    (void)arguments;
    cJSON *status = build_system_status();
    if (!status) {
        return 1;
    }

    int rc = add_json_text_content(content, status);
    cJSON_Delete(status);
    return rc;
}

int list_system_status_resource(cJSON *resources) {
    cJSON *resource = cJSON_CreateObject();
    cJSON_AddStringToObject(resource, "uri", "switch://system/status");
    cJSON_AddStringToObject(resource, "name", "system-status");
    cJSON_AddStringToObject(resource, "title", "System Status");
    cJSON_AddStringToObject(resource, "description", "runtime status for the current Switch sysmodule environment");
    cJSON_AddStringToObject(resource, "mimeType", "application/json");
    cJSON_AddItemToArray(resources, resource);
    return 0;
}

bool match_system_status_resource(const char *uri) {
    return uri && strcmp(uri, "switch://system/status") == 0;
}

int read_system_status_resource(cJSON *contents, const char *uri) {
    cJSON *status = build_system_status();
    cJSON *item = NULL;
    char *text = NULL;

    if (!status) {
        return 1;
    }

    text = cJSON_PrintUnformatted(status);
    item = cJSON_CreateObject();
    cJSON_Delete(status);
    if (!text || !item) {
        free(text);
        cJSON_Delete(item);
        return 1;
    }

    cJSON_AddStringToObject(item, "uri", uri);
    cJSON_AddStringToObject(item, "mimeType", "application/json");
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(contents, item);
    free(text);
    return 0;
}
