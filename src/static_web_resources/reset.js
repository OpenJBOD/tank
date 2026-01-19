document.addEventListener("DOMContentLoaded", () => {
    const statusElement = document.getElementById("reset-status");
    const deviceButton = document.getElementById("reset-device");
    const configButton = document.getElementById("reset-config");

    function setStatus(message, isError = false) {
        if (!statusElement) {
            return;
        }

        statusElement.textContent = message;
        statusElement.style.color = isError ? "#b00020" : "";
    }

    async function postReset(endpoint, confirmMessage, successMessage) {
        if (!window.confirm(confirmMessage)) {
            return;
        }

        try {
            setStatus("Sending request...");
            const response = await fetch(endpoint, { method: "POST" });

            if (!response.ok) {
                const errorText = await response.text();
                throw new Error(`HTTP ${response.status}: ${errorText || "Request failed"}`);
            }

            setStatus(successMessage);
        } catch (error) {
            console.error(`Reset request to ${endpoint} failed:`, error);
            setStatus(`Failed: ${error.message}`, true);
        }
    }

    if (deviceButton) {
        deviceButton.addEventListener("click", () => {
            postReset(
                "/api/reset/device",
                "Restart the device now?",
                "Device reboot accepted. The connection may drop shortly."
            );
        });
    }

    if (configButton) {
        configButton.addEventListener("click", () => {
            postReset(
                "/api/reset/config",
                "Erase all settings, restore defaults, and restart the device?",
                "Configuration reset accepted. The device will reboot after clearing settings."
            );
        });
    }
});
