#include "fs_ops.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../third_party/stb_base64.h"

#define FS_MAX_TOOL_READ_BYTES (64 * 1024)
#define FS_MAX_RESOURCE_READ_BYTES (256 * 1024)

static bool is_safe_path(const char *path) {
    if (!path || path[0] != '/') {
        return false;
    }
    return strstr(path, "..") == NULL;
}

static void add_text_item(cJSON *content, const char *text) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "text");
    cJSON_AddStringToObject(item, "text", text ? text : "");
    cJSON_AddItemToArray(content, item);
}

static const char *stat_type_string(mode_t mode) {
    if (S_ISDIR(mode)) return "directory";
    if (S_ISREG(mode)) return "file";
    return "other";
}

static int add_json_text_item(cJSON *content, cJSON *json) {
    char *text = cJSON_PrintUnformatted(json);
    if (!text) {
        return 1;
    }
    add_text_item(content, text);
    free(text);
    return 0;
}

static bool ensure_dir_recursive(const char *path) {
    char buf[512];
    size_t len = 0;

    if (!is_safe_path(path)) {
        errno = EINVAL;
        return false;
    }

    len = strlen(path);
    if (len >= sizeof(buf)) {
        errno = ENAMETOOLONG;
        return false;
    }

    memcpy(buf, path, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (buf[0] && mkdir(buf, 0777) != 0 && errno != EEXIST) {
                return false;
            }
            buf[i] = '/';
        }
    }

    if (mkdir(buf, 0777) != 0 && errno != EEXIST) {
        return false;
    }
    return true;
}

static bool ensure_parent_dir(const char *path) {
    char buf[512];
    char *slash = NULL;

    if (!is_safe_path(path)) {
        errno = EINVAL;
        return false;
    }

    if (strlen(path) >= sizeof(buf)) {
        errno = ENAMETOOLONG;
        return false;
    }
    strcpy(buf, path);
    slash = strrchr(buf, '/');
    if (!slash || slash == buf) {
        return true;
    }
    *slash = '\0';
    return ensure_dir_recursive(buf);
}

static int build_stat_json(cJSON **out, const char *path) {
    struct stat st;
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return 1;
    }

    if (stat(path, &st) != 0) {
        cJSON_AddStringToObject(obj, "path", path);
        cJSON_AddBoolToObject(obj, "exists", false);
        cJSON_AddStringToObject(obj, "error", strerror(errno));
        *out = obj;
        return 0;
    }

    cJSON_AddStringToObject(obj, "path", path);
    cJSON_AddBoolToObject(obj, "exists", true);
    cJSON_AddStringToObject(obj, "type", stat_type_string(st.st_mode));
    cJSON_AddNumberToObject(obj, "size", (double)st.st_size);
    cJSON_AddNumberToObject(obj, "mtime", (double)st.st_mtime);
    *out = obj;
    return 0;
}

static int list_directory_json(cJSON **out, const char *path) {
    DIR *dir = opendir(path);
    struct dirent *ent = NULL;
    cJSON *arr = cJSON_CreateArray();
    if (!dir || !arr) {
        if (dir) closedir(dir);
        cJSON_Delete(arr);
        return 1;
    }

    while ((ent = readdir(dir)) != NULL) {
        char child[768];
        struct stat st;
        cJSON *entry = NULL;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "name", ent->d_name);
        if (stat(child, &st) == 0) {
            cJSON_AddStringToObject(entry, "type", stat_type_string(st.st_mode));
            cJSON_AddNumberToObject(entry, "size", (double)st.st_size);
        } else {
            cJSON_AddStringToObject(entry, "type", "unknown");
            cJSON_AddStringToObject(entry, "error", strerror(errno));
        }
        cJSON_AddItemToArray(arr, entry);
    }

    closedir(dir);
    *out = arr;
    return 0;
}

static char *read_file_bytes(const char *path, size_t max_bytes, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    long size = 0;
    size_t to_read = 0;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    if ((size_t)size > max_bytes) {
        fclose(f);
        errno = EFBIG;
        return NULL;
    }

    to_read = (size_t)size;
    buf = (char *)malloc(to_read + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (to_read && fread(buf, 1, to_read, f) != to_read) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[to_read] = '\0';
    if (out_len) {
        *out_len = to_read;
    }
    return buf;
}

static const char *guess_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".log") == 0 || strcmp(ext, ".ini") == 0 ||
        strcmp(ext, ".cfg") == 0 || strcmp(ext, ".md") == 0) {
        return "text/plain";
    }
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".xml") == 0) return "application/xml";
    if (strcmp(ext, ".csv") == 0) return "text/csv";
    if (strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0) return "application/yaml";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    return "application/octet-stream";
}

static bool mime_is_textual(const char *mime) {
    return strncmp(mime, "text/", 5) == 0 ||
           strcmp(mime, "application/json") == 0 ||
           strcmp(mime, "application/xml") == 0 ||
           strcmp(mime, "application/yaml") == 0;
}

static bool decode_file_uri(const char *uri, char *path, size_t path_size) {
    const char *src = NULL;
    size_t j = 0;

    if (!uri || strncmp(uri, "file://", 7) != 0) {
        return false;
    }

    src = uri + 7;
    while (*src && *src != '?' && *src != '#') {
        if (j + 1 >= path_size) {
            return false;
        }
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            path[j++] = (char)strtol(hex, NULL, 16);
            src += 3;
            continue;
        }
        path[j++] = *src++;
    }
    path[j] = '\0';
    return is_safe_path(path);
}

int list_fs_ops(cJSON *tools) {
    cJSON *tool = cJSON_CreateObject();
    cJSON *input_schema = cJSON_CreateObject();
    cJSON *properties = cJSON_CreateObject();
    cJSON *required = cJSON_CreateArray();
    cJSON *action = cJSON_CreateObject();
    cJSON *action_enum = cJSON_CreateArray();
    cJSON *path = cJSON_CreateObject();
    cJSON *text = cJSON_CreateObject();
    cJSON *append = cJSON_CreateObject();
    cJSON *max_bytes = cJSON_CreateObject();

    cJSON_AddStringToObject(tool, "name", "fs_ops");
    cJSON_AddStringToObject(tool, "title", "fs_ops");
    cJSON_AddStringToObject(tool, "description", "generic SD-card filesystem operations for automation artifacts and fixtures");

    cJSON_AddStringToObject(action, "type", "string");
    cJSON_AddItemToArray(action_enum, cJSON_CreateString("list"));
    cJSON_AddItemToArray(action_enum, cJSON_CreateString("stat"));
    cJSON_AddItemToArray(action_enum, cJSON_CreateString("read_text"));
    cJSON_AddItemToArray(action_enum, cJSON_CreateString("write_text"));
    cJSON_AddItemToArray(action_enum, cJSON_CreateString("mkdir"));
    cJSON_AddItemToArray(action_enum, cJSON_CreateString("delete"));
    cJSON_AddItemToObject(action, "enum", action_enum);
    cJSON_AddItemToObject(properties, "action", action);

    cJSON_AddStringToObject(path, "type", "string");
    cJSON_AddStringToObject(path, "description", "absolute path on the mounted SD card, for example /switch/switch-mcp-server");
    cJSON_AddItemToObject(properties, "path", path);

    cJSON_AddStringToObject(text, "type", "string");
    cJSON_AddStringToObject(text, "description", "text payload for write_text");
    cJSON_AddItemToObject(properties, "text", text);

    cJSON_AddStringToObject(append, "type", "boolean");
    cJSON_AddStringToObject(append, "description", "append when writing text");
    cJSON_AddItemToObject(properties, "append", append);

    cJSON_AddStringToObject(max_bytes, "type", "number");
    cJSON_AddStringToObject(max_bytes, "description", "maximum bytes to read for read_text");
    cJSON_AddItemToObject(properties, "max_bytes", max_bytes);

    cJSON_AddStringToObject(input_schema, "type", "object");
    cJSON_AddItemToObject(input_schema, "properties", properties);
    cJSON_AddItemToArray(required, cJSON_CreateString("action"));
    cJSON_AddItemToArray(required, cJSON_CreateString("path"));
    cJSON_AddItemToObject(input_schema, "required", required);
    cJSON_AddItemToObject(tool, "inputSchema", input_schema);
    cJSON_AddItemToArray(tools, tool);
    return 0;
}

int call_fs_ops(cJSON *content, const cJSON *arguments) {
    const cJSON *action = NULL;
    const cJSON *path = NULL;
    const cJSON *text = NULL;
    const cJSON *append = NULL;
    const cJSON *max_bytes = NULL;
    cJSON *json = NULL;

    if (!arguments) {
        add_text_item(content, "fs_ops requires action and path");
        return 1;
    }

    action = cJSON_GetObjectItem(arguments, "action");
    path = cJSON_GetObjectItem(arguments, "path");
    text = cJSON_GetObjectItem(arguments, "text");
    append = cJSON_GetObjectItem(arguments, "append");
    max_bytes = cJSON_GetObjectItem(arguments, "max_bytes");

    if (!cJSON_IsString(action) || !cJSON_IsString(path) || !is_safe_path(path->valuestring)) {
        add_text_item(content, "invalid fs_ops arguments");
        return 1;
    }

    if (strcmp(action->valuestring, "stat") == 0) {
        if (build_stat_json(&json, path->valuestring) != 0) {
            add_text_item(content, "stat failed");
            return 1;
        }
        int rc = add_json_text_item(content, json);
        cJSON_Delete(json);
        return rc;
    }

    if (strcmp(action->valuestring, "list") == 0) {
        if (list_directory_json(&json, path->valuestring) != 0) {
            add_text_item(content, strerror(errno));
            return 1;
        }
        int rc = add_json_text_item(content, json);
        cJSON_Delete(json);
        return rc;
    }

    if (strcmp(action->valuestring, "read_text") == 0) {
        size_t len = 0;
        size_t limit = cJSON_IsNumber(max_bytes) && max_bytes->valuedouble > 0 ? (size_t)max_bytes->valuedouble : FS_MAX_TOOL_READ_BYTES;
        char *buf = read_file_bytes(path->valuestring, limit, &len);
        (void)len;
        if (!buf) {
            add_text_item(content, strerror(errno));
            return 1;
        }
        add_text_item(content, buf);
        free(buf);
        return 0;
    }

    if (strcmp(action->valuestring, "write_text") == 0) {
        FILE *f = NULL;
        const char *mode = (cJSON_IsBool(append) && append->valueint) ? "ab" : "wb";
        if (!cJSON_IsString(text)) {
            add_text_item(content, "write_text requires text");
            return 1;
        }
        if (!ensure_parent_dir(path->valuestring)) {
            add_text_item(content, "failed to create parent directories");
            return 1;
        }
        f = fopen(path->valuestring, mode);
        if (!f) {
            add_text_item(content, strerror(errno));
            return 1;
        }
        if (fwrite(text->valuestring, 1, strlen(text->valuestring), f) != strlen(text->valuestring)) {
            fclose(f);
            add_text_item(content, "write failed");
            return 1;
        }
        fclose(f);
        build_stat_json(&json, path->valuestring);
        if (!json) {
            add_text_item(content, "write succeeded");
            return 0;
        }
        int rc = add_json_text_item(content, json);
        cJSON_Delete(json);
        return rc;
    }

    if (strcmp(action->valuestring, "mkdir") == 0) {
        if (!ensure_dir_recursive(path->valuestring)) {
            add_text_item(content, strerror(errno));
            return 1;
        }
        add_text_item(content, "ok");
        return 0;
    }

    if (strcmp(action->valuestring, "delete") == 0) {
        if (remove(path->valuestring) != 0) {
            add_text_item(content, strerror(errno));
            return 1;
        }
        add_text_item(content, "ok");
        return 0;
    }

    add_text_item(content, "unsupported fs_ops action");
    return 1;
}

bool match_file_resource(const char *uri) {
    return uri && strncmp(uri, "file://", 7) == 0;
}

int read_file_resource(cJSON *contents, const char *uri) {
    char path[512];
    struct stat st;
    cJSON *item = NULL;
    const char *mime = NULL;
    size_t len = 0;
    char *buf = NULL;

    if (!decode_file_uri(uri, path, sizeof(path))) {
        return 1;
    }
    if (stat(path, &st) != 0) {
        return 1;
    }

    item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "uri", uri);

    if (S_ISDIR(st.st_mode)) {
        cJSON *listing = NULL;
        char *text = NULL;
        if (list_directory_json(&listing, path) != 0) {
            cJSON_Delete(item);
            return 1;
        }
        text = cJSON_PrintUnformatted(listing);
        cJSON_Delete(listing);
        if (!text) {
            cJSON_Delete(item);
            return 1;
        }
        cJSON_AddStringToObject(item, "mimeType", "inode/directory+json");
        cJSON_AddStringToObject(item, "text", text);
        free(text);
        cJSON_AddItemToArray(contents, item);
        return 0;
    }

    buf = read_file_bytes(path, FS_MAX_RESOURCE_READ_BYTES, &len);
    if (!buf) {
        cJSON_Delete(item);
        return 1;
    }

    mime = guess_mime_type(path);
    cJSON_AddStringToObject(item, "mimeType", mime);
    if (mime_is_textual(mime)) {
        cJSON_AddStringToObject(item, "text", buf);
    } else {
        char *b64 = (char *)malloc(len * 4 / 3 + 8);
        int b64_len = 0;
        if (!b64) {
            free(buf);
            cJSON_Delete(item);
            return 1;
        }
        b64_len = stb_base64_encode((const unsigned char *)buf, (int)len, b64);
        b64[b64_len] = '\0';
        cJSON_AddStringToObject(item, "blob", b64);
        free(b64);
    }

    free(buf);
    cJSON_AddItemToArray(contents, item);
    return 0;
}

int list_file_resource_templates(cJSON *resource_templates) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "uriTemplate", "file:///{path}");
    cJSON_AddStringToObject(item, "name", "sdmc-file");
    cJSON_AddStringToObject(item, "title", "SD Card File");
    cJSON_AddStringToObject(item, "description", "read files or directories from the mounted SD card");
    cJSON_AddItemToArray(resource_templates, item);
    return 0;
}
