#include "streamable_http.h"
#include "registry.h"
#include "../util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char protocol_version[32] = "2025-06-18";
static char session_id[40] = {0};

void gen_session_id(char *buf, int len) {
    for (int i = 0; i < len - 1; ++i) {
        int r = rand() % 62;
        buf[i] = (r < 10) ? ('0' + r) : (r < 36 ? 'A' + r - 10 : 'a' + r - 36);
    }
    buf[len - 1] = '\0';
}

static void send_json_payload(int client_fd, cJSON *payload, const char *extra_headers) {
    char *resp_str = cJSON_PrintUnformatted(payload);
    char header[512];

    if (!resp_str) {
        const char *resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    if (extra_headers && extra_headers[0]) {
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n%s\r\n",
                 extra_headers);
    } else {
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n");
    }

    send(client_fd, header, strlen(header), 0);
    send(client_fd, resp_str, strlen(resp_str), 0);
    free(resp_str);
}

static void send_jsonrpc_result(int client_fd, const cJSON *id, cJSON *result, const char *extra_headers) {
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    cJSON_AddItemToObject(resp, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
    cJSON_AddItemToObject(resp, "result", result);
    send_json_payload(client_fd, resp, extra_headers);
    cJSON_Delete(resp);
}

static void send_jsonrpc_error(int client_fd, const cJSON *id, int code, const char *message, cJSON *data) {
    cJSON *resp = cJSON_CreateObject();
    cJSON *error = cJSON_CreateObject();

    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    cJSON_AddItemToObject(resp, "id", id ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());

    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    if (data) {
        cJSON_AddItemToObject(error, "data", data);
    }

    cJSON_AddItemToObject(resp, "error", error);
    send_json_payload(client_fd, resp, NULL);
    cJSON_Delete(resp);
}

static void send_unknown_tool_result(int client_fd, const cJSON *id, const char *tool_name) {
    cJSON *result = cJSON_CreateObject();
    cJSON *content = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    char text[160];

    snprintf(text, sizeof(text), "Unknown tool: %s", tool_name ? tool_name : "(null)");
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text);
    cJSON_AddItemToArray(content, item);

    cJSON_AddItemToObject(result, "content", content);
    cJSON_AddBoolToObject(result, "isError", 1);
    send_jsonrpc_result(client_fd, id, result, NULL);
}

void handle_http_request(char *req, int req_len, int client_fd) {
    const char *body = NULL;
    cJSON *root = NULL;
    const cJSON *jsonrpc = NULL;
    const cJSON *method = NULL;
    const cJSON *id = NULL;

    (void)req_len;

    if (strstr(req, "GET /.well-known/oauth-authorization-server") == req) {
        const char *resp =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
            "{"
            "\"issuer\":\"http://192.168.1.15:12345\","
            "\"authorization_endpoint\":\"http://192.168.1.15:12345/oauth/authorize\","
            "\"token_endpoint\":\"http://192.168.1.15:12345/oauth/token\","
            "\"response_types_supported\":[\"code\"]"
            "}";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    if (strstr(req, "GET /.well-known/oauth-client-registration") == req) {
        const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{}";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    if (strstr(req, "GET /oauth/authorize") == req || strstr(req, "POST /oauth/authorize") == req) {
        const char *resp =
            "HTTP/1.1 501 Not Implemented\r\nContent-Type: application/json\r\n\r\n"
            "{\"error\":\"OAuth2 authorization not supported\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    if (strstr(req, "GET /oauth/token") == req || strstr(req, "POST /oauth/token") == req) {
        const char *resp =
            "HTTP/1.1 501 Not Implemented\r\nContent-Type: application/json\r\n\r\n"
            "{\"error\":\"OAuth2 token not supported\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    if (strncmp(req, "POST /mcp", 9) != 0) {
        const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nNot Found\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    (void)get_header(req, "MCP-Protocol-Version");
    (void)get_header(req, "Mcp-Session-Id");

    body = strstr(req, "\r\n\r\n");
    if (!body) {
        const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Missing body\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }
    body += 4;

    root = cJSON_Parse(body);
    if (!root) {
        const char *resp = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"error\":\"Invalid JSON\"}\n";
        send(client_fd, resp, strlen(resp), 0);
        return;
    }

    jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
    method = cJSON_GetObjectItem(root, "method");
    id = cJSON_GetObjectItem(root, "id");

    if (!jsonrpc || !cJSON_IsString(jsonrpc) || strcmp(jsonrpc->valuestring, "2.0") != 0 ||
        !method || !cJSON_IsString(method)) {
        cJSON_Delete(root);
        {
            const char *resp = "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n";
            send(client_fd, resp, strlen(resp), 0);
        }
        return;
    }

    if (strcmp(method->valuestring, "initialize") == 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON *caps = cJSON_CreateObject();
        cJSON *resources = cJSON_CreateObject();
        cJSON *tools = cJSON_CreateObject();
        cJSON *info = cJSON_CreateObject();
        char extra_headers[160];

        gen_session_id(session_id, sizeof(session_id));

        cJSON_AddStringToObject(result, "protocolVersion", protocol_version);
        cJSON_AddItemToObject(caps, "resources", resources);
        cJSON_AddItemToObject(caps, "tools", tools);
        cJSON_AddItemToObject(result, "capabilities", caps);

        cJSON_AddStringToObject(info, "name", "switch-mcp-server");
        cJSON_AddStringToObject(info, "title", "Switch MCP Server");
        cJSON_AddStringToObject(info, "version", "1.1.0");
        cJSON_AddItemToObject(result, "serverInfo", info);
        cJSON_AddStringToObject(result, "instructions",
                                "Generic Nintendo Switch MCP server for automation, diagnostics, and artifact capture.");

        snprintf(extra_headers, sizeof(extra_headers),
                 "Mcp-Session-Id: %s\r\nMCP-Protocol-Version: %s",
                 session_id, protocol_version);
        send_jsonrpc_result(client_fd, id, result, extra_headers);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(method->valuestring, "resources/list") == 0 && id) {
        cJSON *result = cJSON_CreateObject();
        cJSON *resources = cJSON_CreateArray();
        registry_list_resources(resources);
        cJSON_AddItemToObject(result, "resources", resources);
        send_jsonrpc_result(client_fd, id, result, NULL);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(method->valuestring, "resources/templates/list") == 0 && id) {
        cJSON *result = cJSON_CreateObject();
        cJSON *resource_templates = cJSON_CreateArray();
        registry_list_resource_templates(resource_templates);
        cJSON_AddItemToObject(result, "resourceTemplates", resource_templates);
        send_jsonrpc_result(client_fd, id, result, NULL);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(method->valuestring, "resources/read") == 0 && id) {
        cJSON *params = cJSON_GetObjectItem(root, "params");
        const cJSON *uri = params ? cJSON_GetObjectItem(params, "uri") : NULL;
        cJSON *result = NULL;
        cJSON *contents = NULL;
        int rc = MCP_REGISTRY_ERROR;

        if (!uri || !cJSON_IsString(uri)) {
            send_jsonrpc_error(client_fd, id, -32602, "Invalid params: resources/read requires a uri string", NULL);
            cJSON_Delete(root);
            return;
        }

        result = cJSON_CreateObject();
        contents = cJSON_CreateArray();
        rc = registry_read_resource(uri->valuestring, contents);
        if (rc == MCP_REGISTRY_NOT_FOUND) {
            cJSON_Delete(contents);
            cJSON_Delete(result);
            send_jsonrpc_error(client_fd, id, -32002, "Resource not found", NULL);
            cJSON_Delete(root);
            return;
        }
        if (rc != MCP_REGISTRY_OK) {
            cJSON_Delete(contents);
            cJSON_Delete(result);
            send_jsonrpc_error(client_fd, id, -32603, "Failed to read resource", NULL);
            cJSON_Delete(root);
            return;
        }

        cJSON_AddItemToObject(result, "contents", contents);
        send_jsonrpc_result(client_fd, id, result, NULL);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(method->valuestring, "tools/list") == 0 && id) {
        cJSON *result = cJSON_CreateObject();
        cJSON *tools = cJSON_CreateArray();
        registry_list_tools(tools);
        cJSON_AddItemToObject(result, "tools", tools);
        send_jsonrpc_result(client_fd, id, result, NULL);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(method->valuestring, "tools/call") == 0 && id) {
        cJSON *params = cJSON_GetObjectItem(root, "params");
        const cJSON *tool_name = params ? cJSON_GetObjectItem(params, "name") : NULL;
        const cJSON *arguments = params ? cJSON_GetObjectItem(params, "arguments") : NULL;
        cJSON *result = cJSON_CreateObject();
        cJSON *content = cJSON_CreateArray();
        int is_error = MCP_REGISTRY_ERROR;

        if (!tool_name || !cJSON_IsString(tool_name)) {
            cJSON_Delete(content);
            cJSON_Delete(result);
            send_jsonrpc_error(client_fd, id, -32602, "Invalid params: tools/call requires a tool name", NULL);
            cJSON_Delete(root);
            return;
        }

        is_error = registry_call_tool(tool_name->valuestring, content, arguments);
        if (is_error == MCP_REGISTRY_NOT_FOUND) {
            cJSON_Delete(content);
            cJSON_Delete(result);
            send_unknown_tool_result(client_fd, id, tool_name->valuestring);
            cJSON_Delete(root);
            return;
        }

        cJSON_AddItemToObject(result, "content", content);
        cJSON_AddBoolToObject(result, "isError", is_error != MCP_REGISTRY_OK);
        send_jsonrpc_result(client_fd, id, result, NULL);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(method->valuestring, "notifications/initialized") == 0) {
        cJSON_Delete(root);
        {
            const char *resp = "HTTP/1.1 202 Accepted\r\nContent-Type: application/json\r\n\r\n";
            send(client_fd, resp, strlen(resp), 0);
        }
        return;
    }

    if (strcmp(method->valuestring, "ping") == 0 && id) {
        cJSON *result = cJSON_CreateObject();
        send_jsonrpc_result(client_fd, id, result, NULL);
        cJSON_Delete(root);
        return;
    }

    log_error("Unsupported method: %s", method->valuestring);
    send_jsonrpc_error(client_fd, id, -32601, "Method not found", NULL);
    cJSON_Delete(root);
}
