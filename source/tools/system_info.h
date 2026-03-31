#pragma once

#include <stdbool.h>
#include "../third_party/cJSON.h"

int list_system_info(cJSON *tools);
int call_system_info(cJSON *content, const cJSON *arguments);

int list_system_status_resource(cJSON *resources);
bool match_system_status_resource(const char *uri);
int read_system_status_resource(cJSON *contents, const char *uri);
