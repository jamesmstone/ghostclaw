#ifndef GHOSTCLAW_PLUGIN_H
#define GHOSTCLAW_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *name;
  const char *version;
  const char *type;
} GhostClawPluginInfo;

typedef struct {
  const char *name;
  const char *description;
  const char *parameters_json;
} GhostClawToolSpec;

typedef struct {
  char *output;
  int success;
  int truncated;
} GhostClawToolResult;

GhostClawPluginInfo *ghostclaw_plugin_info(void);
GhostClawToolSpec *ghostclaw_tool_spec(void);
GhostClawToolResult *ghostclaw_tool_execute(const char *args_json, const char *context_json);
void ghostclaw_tool_result_free(GhostClawToolResult *result);

#ifdef __cplusplus
}
#endif

#endif
