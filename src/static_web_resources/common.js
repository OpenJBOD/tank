/*
 * Shared behaviour for every authenticated page: the "Log out" link and an
 * automatic redirect to /login when an API call reports the session expired.
 */
(function () {
    // Redirect to the login page if any API call comes back 401 (expired/invalid
    // session). The login page does not load this script, so its own /api/login
    // 401 (bad credentials) is unaffected.
    const originalFetch = window.fetch;
    window.fetch = async function (...args) {
        const response = await originalFetch.apply(this, args);
        if (response.status === 401) {
            window.location.href = "/login";
        }
        return response;
    };

    document.addEventListener("DOMContentLoaded", () => {
        const logout = document.getElementById("logout-link");
        if (logout) {
            logout.addEventListener("click", async (event) => {
                event.preventDefault();
                try {
                    await originalFetch("/api/logout", { method: "POST" });
                } catch (_) {
                    /* ignore - we redirect regardless */
                }
                window.location.href = "/login";
            });
        }
    });
})();
