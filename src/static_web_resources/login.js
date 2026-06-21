document.addEventListener("DOMContentLoaded", () => {
    const form = document.getElementById("login-form");
    const statusEl = document.getElementById("login-status");

    function setStatus(message, isError = true) {
        statusEl.textContent = message;
        statusEl.style.color = isError ? "#b00020" : "";
    }

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        setStatus("Signing in...", false);

        const username = document.getElementById("username").value;
        const password = document.getElementById("password").value;

        try {
            const response = await fetch("/api/login", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ username, password })
            });

            if (response.ok) {
                /* The session cookie is set; go to the overview (or the page the
                 * user was sent here from). */
                const dest = new URLSearchParams(window.location.search).get("next") || "/";
                window.location.href = dest;
                return;
            }

            if (response.status === 401) {
                setStatus("Invalid username or password.");
            } else {
                setStatus(`Sign in failed (HTTP ${response.status}).`);
            }
        } catch (error) {
            setStatus("Network error - could not reach the device.");
        }
    });
});
