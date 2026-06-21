/*
 * HTTP header capture registrations for the web server.
 */

#include <zephyr/net/http/server.h>

HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_authorization, "Authorization");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_content_type, "Content-Type");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_cookie, "Cookie");
