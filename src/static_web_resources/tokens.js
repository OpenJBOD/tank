document.addEventListener("DOMContentLoaded", () => {
    const form = document.getElementById("token-form");
    const labelInput = document.getElementById("token-label");
    const listBody = document.getElementById("token-list");
    const newTokenBox = document.getElementById("new-token");
    const newTokenValue = document.getElementById("new-token-value");

    function escapeHtml(s) {
        return String(s).replace(/[&<>"]/g, (c) =>
            ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
    }

    async function loadTokens() {
        try {
            const response = await fetch("/api/tokens");
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            const tokens = await response.json();
            if (!Array.isArray(tokens) || tokens.length === 0) {
                listBody.innerHTML = '<tr><td colspan="3">No tokens.</td></tr>';
                return;
            }
            listBody.innerHTML = tokens.map((t) =>
                `<tr><td><code>${escapeHtml(t.id)}</code></td>` +
                `<td>${escapeHtml(t.label)}</td>` +
                `<td><button data-id="${escapeHtml(t.id)}" class="revoke-btn">Revoke</button></td></tr>`
            ).join("");

            listBody.querySelectorAll(".revoke-btn").forEach((btn) => {
                btn.addEventListener("click", () => revokeToken(btn.getAttribute("data-id")));
            });
        } catch (error) {
            listBody.innerHTML = '<tr><td colspan="3">Failed to load tokens.</td></tr>';
        }
    }

    async function revokeToken(id) {
        if (!window.confirm(`Revoke token ${id}? Anything using it will stop working.`)) {
            return;
        }
        try {
            await fetch(`/api/tokens?id=${encodeURIComponent(id)}`, { method: "DELETE" });
        } catch (_) { /* fall through to reload */ }
        loadTokens();
    }

    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        const label = labelInput.value.trim() || "token";
        try {
            const response = await fetch("/api/tokens", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ label })
            });
            if (!response.ok) {
                const text = await response.text();
                alert(`Could not create token: HTTP ${response.status} ${text}`);
                return;
            }
            const json = await response.json();
            newTokenValue.textContent = json.token;
            newTokenBox.style.display = "block";
            labelInput.value = "";
            loadTokens();
        } catch (error) {
            alert("Could not create token: network error.");
        }
    });

    loadTokens();
});
