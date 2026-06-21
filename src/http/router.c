#include "http/router.h"

#include <zephyr/logging/log.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/util.h>

#include "certificate.h"
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include "firmware_update.h"
#endif
#include "http/auth.h"
#include "http/routes/routes_auth.h"
#include "http/routes/routes_certificates.h"
#include "http/routes/routes_core.h"
#include "http/routes/routes_environment.h"
#include "http/routes/routes_reset.h"
#include "http/routes/routes_power.h"
#include "http/routes/routes_settings.h"
#include "http/routes/routes_tokens.h"
#include "http/routes/routes_users.h"
#include "http/static_assets.h"
#include "settings.h"

LOG_MODULE_REGISTER(tank_http_server, LOG_LEVEL_INF);

#define DEFINE_AUTH_STATIC_RESOURCE(_base, _content_type_literal)                              \
	static struct http_resource_detail_dynamic _base##_auth_resource_detail = {             \
		.common = {                                                                      \
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                        \
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),                        \
			.content_encoding = "gzip",                                               \
			.content_type = _content_type_literal,                                      \
		},                                                                             \
		.cb = authenticated_static_handler,                                            \
		.user_data = &_base##_resource_detail,                                         \
	}

/* Same as above but served without auth (login page). */
#define DEFINE_PUBLIC_STATIC_RESOURCE(_base, _content_type_literal)                             \
	static struct http_resource_detail_dynamic _base##_public_resource_detail = {           \
		.common = {                                                                      \
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                        \
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),                        \
			.content_encoding = "gzip",                                               \
			.content_type = _content_type_literal,                                      \
		},                                                                             \
		.cb = public_static_handler,                                                   \
		.user_data = &_base##_resource_detail,                                         \
	}

/* Every page inlines its own CSS/JS at build time, so only the page documents are
 * served (no standalone /style.css, /*.js routes). The login page is public
 * (pre-auth); the rest require a session.
 */
#if defined(CONFIG_NET_HTTP_SERVICE) || defined(CONFIG_NET_HTTPS_SERVICE)
DEFINE_PUBLIC_STATIC_RESOURCE(login_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(index_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(about_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(network_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(power_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(http_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(environment_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(console_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(users_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(tokens_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(firmware_html_gz, "text/html");
DEFINE_AUTH_STATIC_RESOURCE(reset_html_gz, "text/html");
#endif /* CONFIG_NET_HTTP_SERVICE || CONFIG_NET_HTTPS_SERVICE */

#if defined(CONFIG_NET_HTTP_SERVICE)
static uint16_t test_http_service_port = 80;
HTTP_SERVICE_DEFINE(test_http_service, NULL, &test_http_service_port,
	CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL);

HTTP_RESOURCE_DEFINE(index_html_gz_resource, test_http_service, "/",
	&index_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(about_html_gz_resource, test_http_service, "/about",
	&about_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(network_html_gz_resource, test_http_service, "/network",
	&network_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(power_html_gz_resource, test_http_service, "/power",
	&power_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(http_html_gz_resource, test_http_service, "/http",
	&http_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(environment_html_gz_resource, test_http_service, "/environment",
	&environment_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(console_html_gz_resource, test_http_service, "/console",
	&console_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(users_html_gz_resource, test_http_service, "/users",
	&users_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(tokens_html_gz_resource, test_http_service, "/tokens",
	&tokens_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(firmware_html_gz_resource, test_http_service, "/firmware",
	&firmware_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(reset_html_gz_resource, test_http_service, "/reset",
	&reset_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(login_page_resource, test_http_service, "/login",
	&login_html_gz_public_resource_detail);
HTTP_RESOURCE_DEFINE(login_resource, test_http_service, "/api/login", &login_resource_detail);
HTTP_RESOURCE_DEFINE(logout_resource, test_http_service, "/api/logout", &logout_resource_detail);
HTTP_RESOURCE_DEFINE(tokens_resource, test_http_service, "/api/tokens", &tokens_resource_detail);
HTTP_RESOURCE_DEFINE(device_info_resource, test_http_service, "/api/device_info", &device_info_resource_detail);
HTTP_RESOURCE_DEFINE(led_resource, test_http_service, "/api/led", &led_resource_detail);
HTTP_RESOURCE_DEFINE(power_on_resource, test_http_service, "/api/power/on", &power_on_resource_detail);
HTTP_RESOURCE_DEFINE(power_off_resource, test_http_service, "/api/power/off", &power_off_resource_detail);
HTTP_RESOURCE_DEFINE(power_status_resource, test_http_service, "/api/power", &power_status_resource_detail);
HTTP_RESOURCE_DEFINE(temp_resource, test_http_service, "/api/temp", &temp_resource_detail);
HTTP_RESOURCE_DEFINE(fan_resource, test_http_service, "/api/fan", &fan_resource_detail);
HTTP_RESOURCE_DEFINE(fan_set_resource, test_http_service, "/api/fan/set", &fan_set_resource_detail);
HTTP_RESOURCE_DEFINE(settings_resource, test_http_service, "/api/settings", &settings_resource_detail);
HTTP_RESOURCE_DEFINE(status_resource, test_http_service, "/api/status", &status_resource_detail);
HTTP_RESOURCE_DEFINE(users_resource, test_http_service, "/api/users", &users_resource_detail);
HTTP_RESOURCE_DEFINE(certificates_upload_resource, test_http_service, "/api/certificates/upload", &certificates_upload_resource_detail);
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
HTTP_RESOURCE_DEFINE(firmware_resource, test_http_service, "/api/firmware", &firmware_upload_resource_detail);
#endif
HTTP_RESOURCE_DEFINE(reset_device_resource, test_http_service, "/api/reset/device", &reset_device_resource_detail);
HTTP_RESOURCE_DEFINE(reset_config_resource, test_http_service, "/api/reset/config", &reset_config_resource_detail);
HTTP_RESOURCE_DEFINE(settings_backup_resource, test_http_service, "/api/settings/backup", &settings_backup_resource_detail);
HTTP_RESOURCE_DEFINE(settings_restore_resource, test_http_service, "/api/settings/restore", &settings_restore_resource_detail);
#endif /* CONFIG_NET_HTTP_SERVICE */

#if defined(CONFIG_NET_HTTPS_SERVICE)
static const sec_tag_t sec_tag_list_verify_none[] = {
	OPENJBOD_SERVER_CERTIFICATE_TAG,
};

static uint16_t test_https_service_port = CONFIG_NET_HTTPS_SERVER_SERVICE_PORT;
HTTPS_SERVICE_DEFINE(test_https_service, NULL, &test_https_service_port,
	CONFIG_HTTP_SERVER_MAX_CLIENTS, 10, NULL, NULL, NULL, sec_tag_list_verify_none,
	sizeof(sec_tag_list_verify_none));

HTTP_RESOURCE_DEFINE(index_html_gz_resource_https, test_https_service, "/",
	&index_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(about_html_gz_resource_https, test_https_service, "/about",
	&about_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(network_html_gz_resource_https, test_https_service, "/network",
	&network_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(power_html_gz_resource_https, test_https_service, "/power",
	&power_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(http_html_gz_resource_https, test_https_service, "/http",
	&http_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(environment_html_gz_resource_https, test_https_service, "/environment",
	&environment_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(console_html_gz_resource_https, test_https_service, "/console",
	&console_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(users_html_gz_resource_https, test_https_service, "/users",
	&users_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(tokens_html_gz_resource_https, test_https_service, "/tokens",
	&tokens_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(firmware_html_gz_resource_https, test_https_service, "/firmware",
	&firmware_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(reset_html_gz_resource_https, test_https_service, "/reset",
	&reset_html_gz_auth_resource_detail);
HTTP_RESOURCE_DEFINE(login_page_resource_https, test_https_service, "/login",
	&login_html_gz_public_resource_detail);
HTTP_RESOURCE_DEFINE(login_resource_https, test_https_service, "/api/login", &login_resource_detail);
HTTP_RESOURCE_DEFINE(logout_resource_https, test_https_service, "/api/logout", &logout_resource_detail);
HTTP_RESOURCE_DEFINE(tokens_resource_https, test_https_service, "/api/tokens", &tokens_resource_detail);
HTTP_RESOURCE_DEFINE(device_info_resource_https, test_https_service, "/api/device_info", &device_info_resource_detail);
HTTP_RESOURCE_DEFINE(led_resource_https, test_https_service, "/api/led", &led_resource_detail);
HTTP_RESOURCE_DEFINE(power_on_resource_https, test_https_service, "/api/power/on", &power_on_resource_detail);
HTTP_RESOURCE_DEFINE(power_off_resource_https, test_https_service, "/api/power/off", &power_off_resource_detail);
HTTP_RESOURCE_DEFINE(power_status_resource_https, test_https_service, "/api/power", &power_status_resource_detail);
HTTP_RESOURCE_DEFINE(temp_resource_https, test_https_service, "/api/temp", &temp_resource_detail);
HTTP_RESOURCE_DEFINE(fan_resource_https, test_https_service, "/api/fan", &fan_resource_detail);
HTTP_RESOURCE_DEFINE(fan_set_resource_https, test_https_service, "/api/fan/set", &fan_set_resource_detail);
HTTP_RESOURCE_DEFINE(settings_resource_https, test_https_service, "/api/settings", &settings_resource_detail);
HTTP_RESOURCE_DEFINE(status_resource_https, test_https_service, "/api/status", &status_resource_detail);
HTTP_RESOURCE_DEFINE(users_resource_https, test_https_service, "/api/users", &users_resource_detail);
HTTP_RESOURCE_DEFINE(certificates_upload_resource_https, test_https_service, "/api/certificates/upload", &certificates_upload_resource_detail);
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
HTTP_RESOURCE_DEFINE(firmware_resource_https, test_https_service, "/api/firmware", &firmware_upload_resource_detail);
#endif
HTTP_RESOURCE_DEFINE(reset_device_resource_https, test_https_service, "/api/reset/device", &reset_device_resource_detail);
HTTP_RESOURCE_DEFINE(reset_config_resource_https, test_https_service, "/api/reset/config", &reset_config_resource_detail);
HTTP_RESOURCE_DEFINE(settings_backup_resource_https, test_https_service, "/api/settings/backup", &settings_backup_resource_detail);
HTTP_RESOURCE_DEFINE(settings_restore_resource_https, test_https_service, "/api/settings/restore", &settings_restore_resource_detail);
#endif /* CONFIG_NET_HTTPS_SERVICE */

void tank_http_router_configure(const struct openjbod_settings *settings)
{
	if (settings == NULL) {
		LOG_WRN("HTTP router configuration skipped: settings unavailable");
		return;
	}

#if defined(CONFIG_NET_HTTP_SERVICE)
	if (settings->http.enable_http) {
		test_http_service_port = settings->http.http_port;
		LOG_INF("HTTP service enabled on port %u", test_http_service_port);
	} else {
		test_http_service_port = 0U;
		LOG_INF("HTTP service disabled");
	}
#endif /* CONFIG_NET_HTTP_SERVICE */

#if defined(CONFIG_NET_HTTPS_SERVICE)
	if (settings->http.enable_https) {
		test_https_service_port = settings->http.https_port;
		LOG_INF("HTTPS service enabled on port %u", test_https_service_port);
	} else {
		test_https_service_port = 0U;
		LOG_INF("HTTPS service disabled");
	}
#endif /* CONFIG_NET_HTTPS_SERVICE */
}
