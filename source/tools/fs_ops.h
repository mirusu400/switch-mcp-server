#pragma once

#include <stdbool.h>
#include "../third_party/cJSON.h"

int list_fs_ops(cJSON *tools);
int call_fs_ops(cJSON *content, const cJSON *arguments);

bool match_file_resource(const char *uri);
int read_file_resource(cJSON *contents, const char *uri);
int list_file_resource_templates(cJSON *resource_templates);
