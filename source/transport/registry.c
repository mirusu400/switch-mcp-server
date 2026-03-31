#include "registry.h"

#include <string.h>

#include "../tools/controller.h"
#include "../tools/controller_recorder.h"
#include "../tools/cur_frame.h"
#include "../tools/fs_ops.h"
#include "../tools/system_info.h"

typedef struct {
    const char *name;
    McpToolListFn list;
    McpToolCallFn call;
} McpToolHandler;

typedef struct {
    McpResourceListFn list;
    McpResourceMatchFn match;
    McpResourceReadFn read;
} McpResourceHandler;

typedef struct {
    McpResourceTemplateListFn list;
} McpResourceTemplateHandler;

static int call_cur_frame_tool(cJSON *content, const cJSON *arguments) {
    (void)arguments;
    return call_cur_frame(content);
}

static const McpToolHandler g_tools[] = {
    { "controller", list_controller, call_controller },
    { "cur_frame", list_cur_frame, call_cur_frame_tool },
    { "controller_recorder", list_controller_recorder, call_controller_recorder },
    { "system_info", list_system_info, call_system_info },
    { "fs_ops", list_fs_ops, call_fs_ops },
};

static const McpResourceHandler g_resources[] = {
    { list_cur_frame_resource, match_cur_frame_resource, read_cur_frame_resource },
    { list_system_status_resource, match_system_status_resource, read_system_status_resource },
    { NULL, match_file_resource, read_file_resource },
};

static const McpResourceTemplateHandler g_resource_templates[] = {
    { list_file_resource_templates },
};

void registry_list_tools(cJSON *tools) {
    size_t count = sizeof(g_tools) / sizeof(g_tools[0]);
    for (size_t i = 0; i < count; ++i) {
        if (g_tools[i].list) {
            g_tools[i].list(tools);
        }
    }
}

int registry_call_tool(const char *name, cJSON *content, const cJSON *arguments) {
    size_t count = sizeof(g_tools) / sizeof(g_tools[0]);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(g_tools[i].name, name) == 0) {
            return g_tools[i].call ? g_tools[i].call(content, arguments) : MCP_REGISTRY_ERROR;
        }
    }
    return MCP_REGISTRY_NOT_FOUND;
}

void registry_list_resources(cJSON *resources) {
    size_t count = sizeof(g_resources) / sizeof(g_resources[0]);
    for (size_t i = 0; i < count; ++i) {
        if (g_resources[i].list) {
            g_resources[i].list(resources);
        }
    }
}

int registry_read_resource(const char *uri, cJSON *contents) {
    size_t count = sizeof(g_resources) / sizeof(g_resources[0]);
    for (size_t i = 0; i < count; ++i) {
        if (g_resources[i].match && g_resources[i].match(uri)) {
            return g_resources[i].read ? g_resources[i].read(contents, uri) : MCP_REGISTRY_ERROR;
        }
    }
    return MCP_REGISTRY_NOT_FOUND;
}

void registry_list_resource_templates(cJSON *resource_templates) {
    size_t count = sizeof(g_resource_templates) / sizeof(g_resource_templates[0]);
    for (size_t i = 0; i < count; ++i) {
        if (g_resource_templates[i].list) {
            g_resource_templates[i].list(resource_templates);
        }
    }
}
