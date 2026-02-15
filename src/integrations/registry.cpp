#include "ghostclaw/integrations/registry.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::integrations {

namespace {

const std::vector<Integration> &catalog() {
  static const std::vector<Integration> values = {
      {"GitHub", "dev", "Source control and pull requests"},
      {"GitLab", "dev", "Source control and CI/CD"},
      {"Bitbucket", "dev", "Source control"},
      {"Jira", "project", "Issue tracking"},
      {"Linear", "project", "Issue tracking"},
      {"Asana", "project", "Task management"},
      {"Trello", "project", "Kanban boards"},
      {"Notion", "docs", "Knowledge base"},
      {"Confluence", "docs", "Team documentation"},
      {"Google Docs", "docs", "Collaborative docs"},
      {"Slack", "chat", "Team chat"},
      {"Discord", "chat", "Community chat"},
      {"Microsoft Teams", "chat", "Enterprise chat"},
      {"Telegram", "chat", "Messaging"},
      {"Matrix", "chat", "Federated chat"},
      {"Signal", "chat", "Secure messaging"},
      {"WhatsApp", "chat", "Messaging"},
      {"iMessage", "chat", "Apple messaging"},
      {"Zoom", "meeting", "Video conferencing"},
      {"Google Meet", "meeting", "Video conferencing"},
      {"AWS", "cloud", "Cloud platform"},
      {"GCP", "cloud", "Cloud platform"},
      {"Azure", "cloud", "Cloud platform"},
      {"Cloudflare", "cloud", "Edge network"},
      {"DigitalOcean", "cloud", "Cloud platform"},
      {"Vercel", "deploy", "Frontend deploy"},
      {"Netlify", "deploy", "Frontend deploy"},
      {"Render", "deploy", "App hosting"},
      {"Railway", "deploy", "App hosting"},
      {"Fly.io", "deploy", "Global deploy"},
      {"Kubernetes", "infra", "Container orchestration"},
      {"Docker", "infra", "Containers"},
      {"Terraform", "infra", "Infrastructure as code"},
      {"Pulumi", "infra", "Infrastructure as code"},
      {"Sentry", "observability", "Error tracking"},
      {"Datadog", "observability", "Monitoring"},
      {"Grafana", "observability", "Dashboards"},
      {"Prometheus", "observability", "Metrics"},
      {"New Relic", "observability", "Monitoring"},
      {"PagerDuty", "ops", "Incident response"},
      {"Opsgenie", "ops", "On-call"},
      {"Twilio", "communication", "Messaging APIs"},
      {"Stripe", "finance", "Payments"},
      {"PayPal", "finance", "Payments"},
      {"Shopify", "commerce", "E-commerce"},
      {"Salesforce", "crm", "Customer relationship management"},
      {"HubSpot", "crm", "CRM platform"},
      {"Zendesk", "support", "Customer support"},
      {"Intercom", "support", "Customer messaging"},
      {"Airtable", "data", "Structured data"},
      {"Snowflake", "data", "Data warehouse"},
      {"BigQuery", "data", "Data warehouse"},
      {"Postgres", "data", "Database"},
      {"MySQL", "data", "Database"},
      {"Redis", "data", "Cache and queues"},
  };
  return values;
}

} // namespace

const std::vector<Integration> &IntegrationRegistry::all() const { return catalog(); }

std::vector<Integration> IntegrationRegistry::by_category(const std::string &category) const {
  const std::string needle = common::to_lower(category);
  std::vector<Integration> out;
  for (const auto &integration : catalog()) {
    if (common::to_lower(integration.category) == needle) {
      out.push_back(integration);
    }
  }
  return out;
}

std::optional<Integration> IntegrationRegistry::find(const std::string &name) const {
  const std::string needle = common::to_lower(name);
  for (const auto &integration : catalog()) {
    if (common::to_lower(integration.name) == needle) {
      return integration;
    }
  }
  return std::nullopt;
}

} // namespace ghostclaw::integrations
