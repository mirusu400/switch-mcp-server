#pragma once

#include <stdbool.h>
#include "../third_party/cJSON.h"

enum {
    MCP_REGISTRY_OK = 0,
    MCP_REGISTRY_NOT_FOUND = 1,
    MCP_REGISTRY_ERROR = 2,
};

typedef int (*McpToolListFn)(cJSON *tools);
typedef int (*McpToolCallFn)(cJSON *content, const cJSON *arguments);

typedef int (*McpResourceListFn)(cJSON *resources);
typedef bool (*McpResourceMatchFn)(const char *uri);
typedef int (*McpResourceReadFn)(cJSON *contents, const char *uri);

typedef int (*McpResourceTemplateListFn)(cJSON *resource_templates);

void registry_list_tools(cJSON *tools);
int registry_call_tool(const char *name, cJSON *content, const cJSON *arguments);

void registry_list_resources(cJSON *resources);
int registry_read_resource(const char *uri, cJSON *contents);

void registry_list_resource_templates(cJSON *resource_templates);
